#ifndef QUICKRUN_LOG_H
#define QUICKRUN_LOG_H

void ql_log_init(const char *path, int also_stderr);
void ql_log_close(void);
void ql_log(const char *fmt, ...);

#endif
