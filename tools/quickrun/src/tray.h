#ifndef QUICKRUN_TRAY_H
#define QUICKRUN_TRAY_H

#ifdef _WIN32

int  tray_init(void);
void tray_shutdown(void);
void tray_run_message_loop(void);

void tray_set_log_path(const char *utf8_path);
int  tray_is_elevated(void);

#endif

#endif
