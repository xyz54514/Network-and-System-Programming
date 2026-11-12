#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 9734
#define BACKLOG 20
#define IDLE_TIMEOUT_SEC 15

static void reap_children(int sig)
{
    (void)sig;
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0) {
        /* Reap all finished children. */
    }

    errno = saved_errno;
}

static int parse_number_arg(const char *arg, const char *prefix, int *out)
{
    size_t n = strlen(prefix);
    if (strncmp(arg, prefix, n) != 0)
        return 0;

    char *end = NULL;
    long value = strtol(arg + n, &end, 10);
    if (end == arg + n || *end != '\0')
        return -1;

    *out = (int)value;
    return 1;
}

static void uppercase(char *s)
{
    for (; *s; ++s)
        *s = (char)toupper((unsigned char)*s);
}

static int reply_sysinfo(int fd)
{
    struct utsname u;
    char out[MAX_LINE];

    if (uname(&u) == -1)
        return send_line(fd, "ERROR uname failed");

    snprintf(out, sizeof(out),
             "SYSINFO hostname=%s sysname=%s release=%s machine=%s",
             u.nodename, u.sysname, u.release, u.machine);
    return send_line(fd, out);
}

static int reply_uptime(int fd)
{
    struct sysinfo info;
    char out[MAX_LINE];

    if (sysinfo(&info) == -1)
        return send_line(fd, "ERROR sysinfo failed");

    long total = info.uptime;
    long days = total / 86400;
    long hours = (total % 86400) / 3600;
    long mins = (total % 3600) / 60;

    snprintf(out, sizeof(out), "UPTIME %ld days %ld hours %ld minutes",
             days, hours, mins);
    return send_line(fd, out);
}

static int reply_memory(int fd)
{
    struct sysinfo info;
    char out[MAX_LINE];

    if (sysinfo(&info) == -1)
        return send_line(fd, "ERROR sysinfo failed");

    double total_mb = (double)info.totalram * info.mem_unit / (1024.0 * 1024.0);
    double free_mb = (double)info.freeram * info.mem_unit / (1024.0 * 1024.0);

    snprintf(out, sizeof(out), "MEMORY total=%.1fMB free=%.1fMB",
             total_mb, free_mb);
    return send_line(fd, out);
}

static int reply_disk(int fd)
{
    struct statvfs st;
    char out[MAX_LINE];

    if (statvfs("/", &st) == -1)
        return send_line(fd, "ERROR statvfs failed");

    double total_gb = (double)st.f_blocks * st.f_frsize /
                      (1024.0 * 1024.0 * 1024.0);
    double free_gb = (double)st.f_bavail * st.f_frsize /
                     (1024.0 * 1024.0 * 1024.0);

    snprintf(out, sizeof(out), "DISK / total=%.2fGB free=%.2fGB",
             total_gb, free_gb);
    return send_line(fd, out);
}

static int reply_time(int fd)
{
    time_t now = time(NULL);
    struct tm tm_now;
    char timebuf[64];
    char out[MAX_LINE];

    if (localtime_r(&now, &tm_now) == NULL)
        return send_line(fd, "ERROR localtime failed");

    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_now);
    snprintf(out, sizeof(out), "TIME %s", timebuf);
    return send_line(fd, out);
}

static void handle_client(int client_fd, int robust_mode)
{
    char name[MAX_NAME];
    char line[MAX_LINE];
    char greeting[MAX_LINE];

    if (robust_mode) {
        struct timeval tv;
        tv.tv_sec = IDLE_TIMEOUT_SEC;
        tv.tv_usec = 0;
        if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
            LOGD(1, "setsockopt(SO_RCVTIMEO) failed: %s", strerror(errno));
    }

    LOGD(1, "child PID=%d handling fd=%d", getpid(), client_fd);

    ssize_t n = recv_line(client_fd, name, sizeof(name));

    if (n == -2) {
        if (robust_mode)
            send_line(client_fd, "ERROR name too long; connection closed");
        LOGD(1, "name too long from fd=%d", client_fd);
        return;
    }

    if (n == 0) {
        LOGD(1, "client fd=%d disconnected before sending name", client_fd);
        return;
    }

    if (n < 0) {
        if (robust_mode && (errno == EAGAIN || errno == EWOULDBLOCK))
            send_line(client_fd, "ERROR idle timeout");
        else
            perror("recv_line(name)");
        return;
    }

    if (name[0] == '\0') {
        if (robust_mode)
            send_line(client_fd, "ERROR empty name; connection closed");
        return;
    }

    if (make_greeting(name, greeting, sizeof(greeting)) == -1) {
        send_line(client_fd, "ERROR cannot build greeting");
        return;
    }

    if (send_line(client_fd, greeting) == -1)
        return;

    if (send_line(client_fd,
                  "READY commands: SYSINFO UPTIME MEMORY DISK TIME HELP QUIT") == -1)
        return;

    for (;;) {
        n = recv_line(client_fd, line, sizeof(line));

        if (n == 0) {
            LOGD(1, "client %s disconnected", name);
            break;
        }

        if (n == -2) {
            if (robust_mode) {
                if (send_line(client_fd, "ERROR command too long") == -1)
                    break;
                continue;
            }
            break;
        }

        if (n < 0) {
            if (robust_mode && (errno == EAGAIN || errno == EWOULDBLOCK))
                send_line(client_fd, "ERROR idle timeout; connection closed");
            else
                perror("recv_line(command)");
            break;
        }

        uppercase(line);
        LOGD(2, "client=%s command=%s", name, line);

        if (strcmp(line, "SYSINFO") == 0) {
            if (reply_sysinfo(client_fd) == -1)
                break;
        } else if (strcmp(line, "UPTIME") == 0) {
            if (reply_uptime(client_fd) == -1)
                break;
        } else if (strcmp(line, "MEMORY") == 0) {
            if (reply_memory(client_fd) == -1)
                break;
        } else if (strcmp(line, "DISK") == 0) {
            if (reply_disk(client_fd) == -1)
                break;
        } else if (strcmp(line, "TIME") == 0) {
            if (reply_time(client_fd) == -1)
                break;
        } else if (strcmp(line, "HELP") == 0) {
            if (send_line(client_fd,
                          "HELP SYSINFO UPTIME MEMORY DISK TIME HELP QUIT") == -1)
                break;
        } else if (strcmp(line, "QUIT") == 0) {
            send_line(client_fd, "BYE");
            break;
        } else {
            if (robust_mode) {
                if (send_line(client_fd,
                              "ERROR unknown command; type HELP") == -1)
                    break;
            } else {
                /* Baseline behavior for report comparison: disconnect. */
                send_line(client_fd, "ERROR unknown command; closing");
                break;
            }
        }
    }
}

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT;
    int debug_level = 0;
    int robust_mode = 1;

    const char *env_debug = getenv("DEBUG");
    if (env_debug != NULL)
        debug_level = atoi(env_debug);

    for (int i = 1; i < argc; ++i) {
        int value;
        int rc;

        rc = parse_number_arg(argv[i], "--port=", &value);
        if (rc == 1) {
            port = value;
            continue;
        } else if (rc == -1) {
            fprintf(stderr, "Invalid --port value\n");
            return EXIT_FAILURE;
        }

        rc = parse_number_arg(argv[i], "--debug=", &value);
        if (rc == 1) {
            debug_level = value;
            continue;
        } else if (rc == -1) {
            fprintf(stderr, "Invalid --debug value\n");
            return EXIT_FAILURE;
        }

        rc = parse_number_arg(argv[i], "--robust=", &value);
        if (rc == 1) {
            robust_mode = value ? 1 : 0;
            continue;
        } else if (rc == -1) {
            fprintf(stderr, "Invalid --robust value\n");
            return EXIT_FAILURE;
        }

        fprintf(stderr,
                "Usage: %s [--port=N] [--debug=0..3] [--robust=0|1]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Port must be between 1 and 65535\n");
        return EXIT_FAILURE;
    }

    set_debug_level(debug_level);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int yes = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt(SO_REUSEADDR)");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned short)port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = reap_children;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction(SIGCHLD)");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (robust_mode) {
        struct sigaction sp;
        memset(&sp, 0, sizeof(sp));
        sp.sa_handler = SIG_IGN;
        sigemptyset(&sp.sa_mask);
        if (sigaction(SIGPIPE, &sp, NULL) == -1) {
            perror("sigaction(SIGPIPE)");
            close(server_fd);
            return EXIT_FAILURE;
        }
    }

    printf("Server PID=%d listening on 0.0.0.0:%d backlog=%d robust=%s debug=%d\n",
           getpid(), port, BACKLOG, robust_mode ? "ON" : "OFF", debug_level);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd == -1) {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }

        char ip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        LOGD(1, "accepted %s:%u fd=%d", ip,
             (unsigned)ntohs(client_addr.sin_port), client_fd);

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            close(server_fd);
            handle_client(client_fd, robust_mode);
            close(client_fd);
            _exit(EXIT_SUCCESS);
        }

        LOGD(1, "parent created child PID=%d for fd=%d", pid, client_fd);
        close(client_fd);
    }
}
