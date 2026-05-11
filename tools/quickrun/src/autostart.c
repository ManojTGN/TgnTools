#include "autostart.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#ifdef __APPLE__
#  include <mach-o/dyld.h>
#endif

static void set_msg(char *buf, size_t cap, const char *fmt, ...) {
    if (!buf || cap == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
}

/* ---------- self-path resolution ---------- */

#ifdef _WIN32
static int self_path_w(wchar_t *out, size_t cap) {
    DWORD n = GetModuleFileNameW(NULL, out, (DWORD)cap);
    return (n > 0 && n < cap) ? 0 : -1;
}
#else
static int self_path(char *out, size_t cap) {
#  ifdef __APPLE__
    uint32_t sz = (uint32_t)cap;
    if (_NSGetExecutablePath(out, &sz) != 0) return -1;
    /* _NSGetExecutablePath may return a non-canonical path; realpath fixes it. */
    char resolved[4096];
    if (realpath(out, resolved)) {
        strncpy(out, resolved, cap - 1);
        out[cap - 1] = 0;
    }
    return 0;
#  else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return -1;
    out[n] = 0;
    return 0;
#  endif
}
#endif

/* ---------- Windows ---------- */

#ifdef _WIN32

#define RUN_KEY_PATH  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_KEY_NAME  L"quickrun"
#define RUN_KEY_DESC  "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\quickrun"

int quickrun_autostart_install(char *where_out, size_t where_sz,
                               char *err_out,   size_t err_sz)
{
    wchar_t exe[MAX_PATH * 2];
    if (self_path_w(exe, sizeof(exe) / sizeof(exe[0])) != 0) {
        set_msg(err_out, err_sz, "GetModuleFileNameW failed (%lu)", GetLastError());
        return -1;
    }

    wchar_t quoted[MAX_PATH * 2 + 4];
    swprintf(quoted, sizeof(quoted) / sizeof(quoted[0]), L"\"%ls\"", exe);

    HKEY key;
    LSTATUS s = RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH,
        0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (s != ERROR_SUCCESS) {
        set_msg(err_out, err_sz, "RegCreateKeyEx failed (%ld)", s);
        return -1;
    }

    DWORD bytes = (DWORD)((wcslen(quoted) + 1) * sizeof(wchar_t));
    s = RegSetValueExW(key, RUN_KEY_NAME, 0, REG_SZ,
        (const BYTE *)quoted, bytes);
    RegCloseKey(key);
    if (s != ERROR_SUCCESS) {
        set_msg(err_out, err_sz, "RegSetValueEx failed (%ld)", s);
        return -1;
    }

    set_msg(where_out, where_sz, RUN_KEY_DESC);
    return 0;
}

int quickrun_autostart_uninstall(char *where_out, size_t where_sz,
                                 char *err_out,   size_t err_sz)
{
    HKEY key;
    LSTATUS s = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH,
        0, KEY_SET_VALUE, &key);
    if (s == ERROR_FILE_NOT_FOUND) {
        set_msg(where_out, where_sz, RUN_KEY_DESC);
        return 0; /* nothing to remove */
    }
    if (s != ERROR_SUCCESS) {
        set_msg(err_out, err_sz, "RegOpenKeyEx failed (%ld)", s);
        return -1;
    }

    s = RegDeleteValueW(key, RUN_KEY_NAME);
    RegCloseKey(key);
    if (s != ERROR_SUCCESS && s != ERROR_FILE_NOT_FOUND) {
        set_msg(err_out, err_sz, "RegDeleteValue failed (%ld)", s);
        return -1;
    }

    set_msg(where_out, where_sz, RUN_KEY_DESC);
    return 0;
}

#else /* POSIX */

/* mkdir -p style. Returns 0 on success or if the dir already exists. */
static int mkdir_p(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

#ifdef __APPLE__

#define PLIST_LABEL "com.tgn.quickrun"

static int mac_plist_path(char *out, size_t cap) {
    const char *home = getenv("HOME");
    if (!home || !*home) return -1;
    int n = snprintf(out, cap, "%s/Library/LaunchAgents/" PLIST_LABEL ".plist", home);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

int quickrun_autostart_install(char *where_out, size_t where_sz,
                               char *err_out,   size_t err_sz)
{
    char exe[2048];
    if (self_path(exe, sizeof(exe)) != 0) {
        set_msg(err_out, err_sz, "failed to resolve self path");
        return -1;
    }

    char path[2048];
    if (mac_plist_path(path, sizeof(path)) != 0) {
        set_msg(err_out, err_sz, "HOME not set");
        return -1;
    }

    /* Ensure parent dir exists. */
    char dir[2048];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; mkdir_p(dir); }

    FILE *f = fopen(path, "w");
    if (!f) {
        set_msg(err_out, err_sz, "open %s: %s", path, strerror(errno));
        return -1;
    }
    fprintf(f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
        " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "  <key>Label</key>            <string>" PLIST_LABEL "</string>\n"
        "  <key>ProgramArguments</key> <array><string>%s</string></array>\n"
        "  <key>RunAtLoad</key>        <true/>\n"
        "  <key>KeepAlive</key>        <true/>\n"
        "  <key>StandardOutPath</key>  <string>/tmp/quickrun.out.log</string>\n"
        "  <key>StandardErrorPath</key><string>/tmp/quickrun.err.log</string>\n"
        "</dict></plist>\n",
        exe);
    fclose(f);

    /* Best-effort: load now so the user gets autostart immediately. */
    char cmd[2200];
    snprintf(cmd, sizeof(cmd),
        "launchctl unload '%s' >/dev/null 2>&1; "
        "launchctl load -w '%s' >/dev/null 2>&1",
        path, path);
    (void)system(cmd);

    set_msg(where_out, where_sz, "%s", path);
    return 0;
}

int quickrun_autostart_uninstall(char *where_out, size_t where_sz,
                                 char *err_out,   size_t err_sz)
{
    char path[2048];
    if (mac_plist_path(path, sizeof(path)) != 0) {
        set_msg(err_out, err_sz, "HOME not set");
        return -1;
    }

    char cmd[2200];
    snprintf(cmd, sizeof(cmd),
        "launchctl unload '%s' >/dev/null 2>&1", path);
    (void)system(cmd);

    if (unlink(path) != 0 && errno != ENOENT) {
        set_msg(err_out, err_sz, "unlink %s: %s", path, strerror(errno));
        return -1;
    }

    set_msg(where_out, where_sz, "%s", path);
    return 0;
}

#else /* Linux / other POSIX */

static int linux_autostart_path(char *out, size_t cap) {
    const char *xdg  = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int n;
    if (xdg && *xdg) {
        n = snprintf(out, cap, "%s/autostart/quickrun.desktop", xdg);
    } else if (home && *home) {
        n = snprintf(out, cap, "%s/.config/autostart/quickrun.desktop", home);
    } else {
        return -1;
    }
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

int quickrun_autostart_install(char *where_out, size_t where_sz,
                               char *err_out,   size_t err_sz)
{
    char exe[2048];
    if (self_path(exe, sizeof(exe)) != 0) {
        set_msg(err_out, err_sz, "failed to resolve self path");
        return -1;
    }

    char path[2048];
    if (linux_autostart_path(path, sizeof(path)) != 0) {
        set_msg(err_out, err_sz, "HOME / XDG_CONFIG_HOME not set");
        return -1;
    }

    char dir[2048];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; if (mkdir_p(dir) != 0) {
        set_msg(err_out, err_sz, "mkdir %s: %s", dir, strerror(errno));
        return -1;
    }}

    FILE *f = fopen(path, "w");
    if (!f) {
        set_msg(err_out, err_sz, "open %s: %s", path, strerror(errno));
        return -1;
    }
    fprintf(f,
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=quickrun\n"
        "Comment=Global-hotkey daemon\n"
        "Exec=%s\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n"
        "NoDisplay=true\n",
        exe);
    fclose(f);

    set_msg(where_out, where_sz, "%s", path);
    return 0;
}

int quickrun_autostart_uninstall(char *where_out, size_t where_sz,
                                 char *err_out,   size_t err_sz)
{
    char path[2048];
    if (linux_autostart_path(path, sizeof(path)) != 0) {
        set_msg(err_out, err_sz, "HOME / XDG_CONFIG_HOME not set");
        return -1;
    }

    if (unlink(path) != 0 && errno != ENOENT) {
        set_msg(err_out, err_sz, "unlink %s: %s", path, strerror(errno));
        return -1;
    }

    set_msg(where_out, where_sz, "%s", path);
    return 0;
}

#endif /* __APPLE__ vs Linux */

#endif /* _WIN32 vs POSIX */
