#include "action.h"
#include "config.h"
#include "json.h"
#include "keyspec.h"
#include "log.h"
#include "tray.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uiohook.h>

#ifdef _WIN32
#  include <windows.h>
#  include <process.h>
#else
#  include <fcntl.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#ifndef QUICKRUN_VERSION
#  define QUICKRUN_VERSION "dev"
#endif
#ifndef QUICKRUN_COMMIT
#  define QUICKRUN_COMMIT "unknown"
#endif

static quickrun_config *g_config = NULL;

#ifdef _WIN32
static CRITICAL_SECTION g_config_lock;
static int              g_config_lock_inited = 0;
#  define CONFIG_LOCK_INIT() do { InitializeCriticalSection(&g_config_lock); g_config_lock_inited = 1; } while (0)
#  define CONFIG_LOCK()      do { if (g_config_lock_inited) EnterCriticalSection(&g_config_lock); } while (0)
#  define CONFIG_UNLOCK()    do { if (g_config_lock_inited) LeaveCriticalSection(&g_config_lock); } while (0)
#else
#  define CONFIG_LOCK_INIT() ((void)0)
#  define CONFIG_LOCK()      ((void)0)
#  define CONFIG_UNLOCK()    ((void)0)
#endif

static void on_signal(int sig) {
    (void)sig;
    hook_stop();
}

static void dispatch_event(uiohook_event *const event) {
    if (event->type != EVENT_KEY_PRESSED) return;

    CONFIG_LOCK();
    if (g_config) {
        uint16_t keycode = event->data.keyboard.keycode;
        uint16_t mask    = event->mask;

        for (size_t i = 0; i < g_config->binding_count; i++) {
            binding *b = &g_config->bindings[i];
            if (keyspec_matches(&b->key, keycode, mask)) {
                ql_log("hotkey: %s fired", b->key.spec);
                action_execute(&b->act);
                break;
            }
        }
    }
    CONFIG_UNLOCK();
}

void quickrun_reload_config(void) {
    char err[256] = {0};
    quickrun_config *next = quickrun_config_load(NULL, err, sizeof(err));
    if (!next) {
        ql_log("reload: failed - %s", err[0] ? err : "config_load returned NULL");
        return;
    }

    CONFIG_LOCK();
    quickrun_config *prev = g_config;
    g_config = next;
    CONFIG_UNLOCK();

    quickrun_config_free(prev);
    ql_log("reload: loaded %zu binding%s",
        next->binding_count, next->binding_count == 1 ? "" : "s");
    if (err[0]) ql_log("reload warning: %s", err);
}

#ifdef _WIN32
static unsigned __stdcall hook_thread_proc(void *arg) {
    (void)arg;
    int rc = hook_run();
    if (rc != UIOHOOK_SUCCESS) ql_log("hook_run returned %d", rc);
    /* Tell the main-thread message loop to exit. */
    PostQuitMessage(0);
    return 0;
}

static void try_attach_parent_console(void) {
    /*
     * GUI-subsystem binaries do not allocate a console. If launched from cmd
     * or PowerShell we can attach to the parent's console and route logs there
     * for free. Failing to attach is fine - logs still flow to the log file.
     */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

static int take_over_existing_instance(void) {
    HWND existing = FindWindowW(L"QuickrunTrayWnd", NULL);
    if (!existing) return 0;

    /* Tell the running instance to clean up and exit. */
    SendMessageW(existing, WM_CLOSE, 0, 0);

    /* Wait up to ~5 seconds for the previous instance to fully exit before
     * we try to claim the tray slot ourselves. */
    for (int i = 0; i < 50; i++) {
        if (!FindWindowW(L"QuickrunTrayWnd", NULL)) return 1;
        Sleep(100);
    }
    return 1;
}
#endif

#ifndef _WIN32
static int detach_to_background(void) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0); /* parent exits, returning shell prompt */
    setsid();

    /* Close stdio - everything goes to log_file from here on. */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        if (devnull > 2) close(devnull);
    }
    return 0;
}
#endif

static void print_usage(void) {
    fprintf(stderr,
        "quickrun - global-hotkey daemon\n"
        "\n"
        "Usage: quickrun [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --config             print resolved config (path + contents)\n"
        "  --foreground, -f     stay attached to terminal, no tray icon\n"
#ifndef _WIN32
        "  --detach, -d         fork+setsid into the background (POSIX)\n"
#endif
        "  --version, -V        print version\n"
        "  --help, -h           show this help\n");
}

static int print_config_view(void) {
    char *path = quickrun_config_resolve_path(NULL);
    if (!path) {
        fprintf(stderr, "quickrun: cannot resolve config path\n");
        return 1;
    }
    printf("config: %s\n", path);
    FILE *f = fopen(path, "rb");
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) fwrite(buf, 1, n, stdout);
        fclose(f);
        putchar('\n');
    } else {
        printf("(file does not exist - quickrun has no bindings to act on)\n");
    }
    free(path);
    return 0;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    try_attach_parent_console();
#endif

    int foreground = 0;
    int detach     = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
            printf("quickrun %s (%s)\n", QUICKRUN_VERSION, QUICKRUN_COMMIT);
            return 0;
        }
        if (strcmp(a, "--config") == 0) {
            return print_config_view();
        }
        if (strcmp(a, "--foreground") == 0 || strcmp(a, "-f") == 0) {
            foreground = 1;
            continue;
        }
        if (strcmp(a, "--detach") == 0 || strcmp(a, "-d") == 0) {
            detach = 1;
            continue;
        }
        if (a[0] == '-') {
            fprintf(stderr, "quickrun: unknown option '%s'\n", a);
            return 2;
        }
    }

#ifdef _WIN32
    int restarted = take_over_existing_instance();
#endif

    char err[256] = {0};
    g_config = quickrun_config_load(NULL, err, sizeof(err));
    if (!g_config) {
        fprintf(stderr, "quickrun: %s\n", err[0] ? err : "config load failed");
        return 1;
    }

    char *resolved_log_path = NULL;
    const char *log_path = g_config->log_file;
    if (!log_path || !*log_path) {
        resolved_log_path = quickrun_default_log_path();
        log_path = resolved_log_path;
    }
    ql_log_init(log_path, foreground);
    if (log_path) ql_log("log file: %s", log_path);
    if (err[0])   ql_log("config warning: %s", err);

#ifdef _WIN32
    if (restarted) ql_log("replaced previous quickrun instance");
#endif

    if (g_config->binding_count == 0) {
        ql_log("no bindings configured - quickrun has nothing to do. exiting.");
        quickrun_config_free(g_config);
        ql_log_close();
        return 0;
    }

    ql_log("loaded %zu binding%s",
        g_config->binding_count, g_config->binding_count == 1 ? "" : "s");

    CONFIG_LOCK_INIT();

#ifndef _WIN32
    if (detach) {
        if (detach_to_background() != 0) {
            ql_log("detach failed");
            return 1;
        }
    }
#else
    (void)detach;
#endif

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    hook_set_dispatch_proc(dispatch_event);

#ifdef _WIN32
    if (foreground) {
        ql_log("starting hook (foreground, ctrl+c to exit)");
        hook_run();
    } else {
        ql_log("starting hook (tray icon - right-click to quit)");
        if (tray_init() != 0) {
            ql_log("tray init failed - falling back to foreground");
            hook_run();
        } else {
            HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, hook_thread_proc, NULL, 0, NULL);
            if (!thread) {
                ql_log("could not start hook thread");
                tray_shutdown();
                quickrun_config_free(g_config);
                ql_log_close();
                return 1;
            }
            tray_run_message_loop();
            hook_stop();
            WaitForSingleObject(thread, 5000);
            CloseHandle(thread);
            tray_shutdown();
        }
    }
#else
    ql_log("starting hook");
    hook_run();
#endif

    ql_log("shutting down");
    CONFIG_LOCK();
    quickrun_config *to_free = g_config;
    g_config = NULL;
    CONFIG_UNLOCK();
    quickrun_config_free(to_free);
    ql_log_close();
    free(resolved_log_path);
    return 0;
}
