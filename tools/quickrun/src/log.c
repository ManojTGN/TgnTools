#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *g_log_file    = NULL;
static int   g_log_stderr  = 1;

void ql_log_init(const char *path, int also_stderr) {
    g_log_stderr = also_stderr ? 1 : 0;
    if (!path || !*path) return;

    FILE *f = fopen(path, "a");
    if (!f) return;
    g_log_file = f;
}

void ql_log_close(void) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

static void write_timestamp(FILE *f) {
    time_t now = time(NULL);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    fputs(buf, f);
    fputs("  ", f);
}

void ql_log(const char *fmt, ...) {
    if (!g_log_file && !g_log_stderr) return;

    va_list ap;
    char    line[1024];

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (g_log_stderr) {
        write_timestamp(stderr);
        fputs(line, stderr);
        fputc('\n', stderr);
        fflush(stderr);
    }
    if (g_log_file) {
        write_timestamp(g_log_file);
        fputs(line, g_log_file);
        fputc('\n', g_log_file);
        fflush(g_log_file);
    }
}
