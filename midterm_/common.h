#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <sys/types.h>

#define MAX_NAME 64
#define MAX_LINE 512

/* Shared by client and server through libcommon.so */
ssize_t send_all(int fd, const void *buf, size_t len);
int send_line(int fd, const char *text);

/*
 * recv_line return values:
 *   > 0 : number of bytes stored (not including '\n')
 *     0 : peer closed the connection before sending a line
 *    -1 : system-call error (check errno)
 *    -2 : input line was longer than the supplied buffer; rest was discarded
 */
ssize_t recv_line(int fd, char *buf, size_t size);

void trim_newline(char *s);
int make_greeting(const char *name, char *out, size_t out_size);

void set_debug_level(int level);
int get_debug_level(void);
void debug_log_impl(int level, const char *file, int line,
                    const char *func, const char *fmt, ...);

#ifdef ENABLE_DEBUG
#define LOGD(level, fmt, ...) \
    debug_log_impl((level), __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__)
#else
#define LOGD(level, fmt, ...) ((void)0)
#endif

#endif
