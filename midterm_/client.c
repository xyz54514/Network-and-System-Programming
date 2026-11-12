#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_SERVER "127.0.0.1"
#define DEFAULT_PORT 9734

static int read_stdin_line(char *buf, size_t size)
{
    if (fgets(buf, (int)size, stdin) == NULL)
        return 0;

    if (strchr(buf, '\n') == NULL) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {
            /* Drain overly long local input safely. */
        }
    }

    trim_newline(buf);
    return 1;
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

int main(int argc, char **argv)
{
    const char *server_ip = DEFAULT_SERVER;
    int port = DEFAULT_PORT;
    int debug_level = 0;

    const char *env_debug = getenv("DEBUG");
    if (env_debug != NULL)
        debug_level = atoi(env_debug);

    for (int i = 1; i < argc; ++i) {
        int value;
        int rc = parse_number_arg(argv[i], "--port=", &value);

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

        if (strncmp(argv[i], "--server=", 9) == 0) {
            server_ip = argv[i] + 9;
            continue;
        }

        fprintf(stderr,
                "Usage: %s [--server=IPv4] [--port=N] [--debug=0..3]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    set_debug_level(debug_level);

    /* Do not let an unexpected peer close terminate the client via SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, server_ip, &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", server_ip);
        close(sockfd);
        return EXIT_FAILURE;
    }

    LOGD(1, "connecting to %s:%d", server_ip, port);

    if (connect(sockfd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    char name[256];
    char line[MAX_LINE];

    printf("Connected to %s:%d\n", server_ip, port);
    printf("Enter your name: ");
    fflush(stdout);

    if (!read_stdin_line(name, sizeof(name))) {
        fprintf(stderr, "No name provided\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (send_line(sockfd, name) == -1) {
        perror("send name");
        close(sockfd);
        return EXIT_FAILURE;
    }

    ssize_t n = recv_line(sockfd, line, sizeof(line));
    if (n <= 0) {
        if (n < 0)
            perror("recv greeting");
        else
            fprintf(stderr, "Server closed connection\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Server: %s\n", line);

    /* A robustness error may close the connection after one response. */
    if (strncmp(line, "ERROR", 5) == 0) {
        close(sockfd);
        return EXIT_FAILURE;
    }

    n = recv_line(sockfd, line, sizeof(line));
    if (n <= 0) {
        fprintf(stderr, "Server closed connection before menu\n");
        close(sockfd);
        return EXIT_FAILURE;
    }
    printf("Server: %s\n", line);

    for (;;) {
        char command[256];

        printf("command> ");
        fflush(stdout);

        if (!read_stdin_line(command, sizeof(command))) {
            printf("\nInput ended; closing.\n");
            break;
        }

        if (command[0] == '\0')
            continue;

        if (send_line(sockfd, command) == -1) {
            perror("send command");
            break;
        }

        n = recv_line(sockfd, line, sizeof(line));
        if (n == 0) {
            printf("Server closed connection.\n");
            break;
        }
        if (n < 0) {
            perror("recv response");
            break;
        }

        printf("Server: %s\n", line);

        if (strcmp(line, "BYE") == 0)
            break;
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
