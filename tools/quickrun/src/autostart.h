#ifndef QUICKRUN_AUTOSTART_H
#define QUICKRUN_AUTOSTART_H

#include <stddef.h>

/*
 * Install / uninstall quickrun as a user-level autostart entry.
 *
 *   Windows : HKCU\Software\Microsoft\Windows\CurrentVersion\Run\quickrun
 *   macOS   : ~/Library/LaunchAgents/com.tgn.quickrun.plist (launchctl load)
 *   Linux   : $XDG_CONFIG_HOME/autostart/quickrun.desktop
 *
 * On success, returns 0 and writes the resolved entry location into
 * `where_out` (UTF-8). On failure, returns non-zero and writes a human
 * message into `err_out`. Buffers may be NULL.
 *
 * `uninstall` is idempotent: removing an absent entry succeeds.
 */
int quickrun_autostart_install(char *where_out, size_t where_sz,
                               char *err_out,   size_t err_sz);

int quickrun_autostart_uninstall(char *where_out, size_t where_sz,
                                 char *err_out,   size_t err_sz);

#endif
