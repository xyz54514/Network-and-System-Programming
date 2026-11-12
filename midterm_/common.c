#include "common.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_debug_level = 0;

ssize_t send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = write(fd, p + sent, len - sent);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        sent += (size_t)n;
    }

    return (ssize_t)sent;
}

int send_line(int fd, const char *text)
{
    size_t len = strlen(text);

    if (send_all(fd, text, len) < 0)
        return -1;

    if (send_all(fd, "\n", 1) < 0)
        return -1;

    return 0;
}

ssize_t recv_line(int fd, char *buf, size_t size)
{
    size_t used = 0;
    int too_long = 0;

    if (buf == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        char ch;
        ssize_t n = read(fd, &ch, 1);

        if (n == 0) {
            if (used == 0 && !too_long)
                return 0;
            break;
        }

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (ch == '\n')
            break;

        if (ch == '\r')
            continue;

        if (used + 1 < size) {
            buf[used++] = ch;
        } else {
            /* Keep reading until newline so the next request starts cleanly. */
            too_long = 1;
        }
    }

    buf[used] = '\0';

    if (too_long)
        return -2;

    return (ssize_t)used;
}

void trim_newline(char *s)
{
    if (s == NULL)
        return;

    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        --len;
    }
}

int make_greeting(const char *name, char *out, size_t out_size)
{
    if (name == NULL || out == NULL || out_size == 0)
        return -1;

    int n = snprintf(out, out_size, "Hello, %s!", name);
    if (n < 0 || (size_t)n >= out_size)
        return -1;

    return 0;
}

void set_debug_level(int level)
{
    if (level < 0)
        level = 0;
    if (level > 3)
        level = 3;
    g_debug_level = level;
}

int get_debug_level(void)
{
    return g_debug_level;
}

void debug_log_impl(int level, const char *file, int line,
                    const char *func, const char *fmt, ...)
{
    if (level > g_debug_level)
        return;

    fprintf(stderr, "[DEBUG%d] %s:%d %s() | ", level, file, line, func);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
}
