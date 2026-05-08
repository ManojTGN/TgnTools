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

#define QUICKRUN_DETACHED_ENV L"QUICKRUN_DETACHED"

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
    PostQuitMessage(0);
    return 0;
}

#define QUICKRUN_SINGLE_INSTANCE_NAME L"Local\\QuickrunSingleInstance"

static HANDLE g_single_instance_mutex = NULL;

int quickrun_acquire_single_instance(void) {
    if (g_single_instance_mutex) return 1;
    g_single_instance_mutex = CreateMutexW(NULL, FALSE, QUICKRUN_SINGLE_INSTANCE_NAME);
    if (!g_single_instance_mutex) return 0;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_single_instance_mutex);
        g_single_instance_mutex = NULL;
        return 0;
    }
    return 1;
}

void quickrun_release_single_instance(void) {
    if (g_single_instance_mutex) {
        CloseHandle(g_single_instance_mutex);
        g_single_instance_mutex = NULL;
    }
}

/*
 * Probe-only check used by the launcher. SYNCHRONIZE access is permitted
 * across UAC integrity levels with the default DACL/MIC policy, so a medium-IL
 * launcher correctly observes a high-IL daemon's mutex.
 */
static int single_instance_exists(void) {
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, QUICKRUN_SINGLE_INSTANCE_NAME);
    if (!h) return 0;
    CloseHandle(h);
    return 1;
}

static int already_detached(void) {
    wchar_t buf[8];
    DWORD n = GetEnvironmentVariableW(QUICKRUN_DETACHED_ENV, buf,
        sizeof(buf) / sizeof(buf[0]));
    return n > 0;
}

/*
 * Spawn ourselves with explorer.exe as the parent process. That's the only
 * reliable way out of Windows Terminal's job object - CREATE_BREAKAWAY_FROM_JOB
 * needs the parent job to allow breakaway, which WT's job does not.
 *
 * Re-parenting via PROC_THREAD_ATTRIBUTE_PARENT_PROCESS sets the new process's
 * lineage to Explorer; whatever job WT had us in does not apply to the child.
 */
static HANDLE open_explorer_for_create(void) {
    HWND shell = GetShellWindow();
    if (!shell) return NULL;
    DWORD pid = 0;
    GetWindowThreadProcessId(shell, &pid);
    if (!pid) return NULL;
    return OpenProcess(PROCESS_CREATE_PROCESS, FALSE, pid);
}

static int spawn_detached_self(void) {
    wchar_t exe_path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, exe_path,
        sizeof(exe_path) / sizeof(exe_path[0]));
    if (n == 0 || n >= sizeof(exe_path) / sizeof(exe_path[0])) return -1;

    wchar_t cmdline[MAX_PATH * 2 + 16];
    swprintf(cmdline, sizeof(cmdline) / sizeof(cmdline[0]), L"\"%ls\"", exe_path);

    SetEnvironmentVariableW(QUICKRUN_DETACHED_ENV, L"1");

    int spawned = 0;

    HANDLE explorer = open_explorer_for_create();
    if (explorer) {
        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
        LPPROC_THREAD_ATTRIBUTE_LIST attrs =
            (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
        if (attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) {
            if (UpdateProcThreadAttribute(attrs, 0,
                    PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
                    &explorer, sizeof(explorer), NULL, NULL))
            {
                STARTUPINFOEXW siex = { 0 };
                siex.StartupInfo.cb  = sizeof(siex);
                siex.lpAttributeList = attrs;
                PROCESS_INFORMATION pi = { 0 };

                if (CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT
                        | DETACHED_PROCESS
                        | CREATE_NEW_PROCESS_GROUP,
                        NULL, NULL,
                        (LPSTARTUPINFOW)&siex, &pi))
                {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    spawned = 1;
                }
            }
            DeleteProcThreadAttributeList(attrs);
        }
        free(attrs);
        CloseHandle(explorer);
    }

    /* Fallback: try CREATE_BREAKAWAY_FROM_JOB (works only if parent job allows). */
    if (!spawned) {
        STARTUPINFOW si = { 0 };
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = { 0 };
        if (CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB,
                NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            spawned = 1;
        }
    }

    SetEnvironmentVariableW(QUICKRUN_DETACHED_ENV, NULL);
    return spawned ? 0 : -1;
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
        "  --version, -V        print version\n"
        "  --help, -h           show this help\n"
        "\n"
        "Default behavior (no args): launches detached in the background, with\n"
        "a tray icon. Re-running quickrun while it's already running is a no-op.\n"
        "Left-click the tray icon to open a console tailing the log file;\n"
        "right-click for the menu (Open file location / Reload config / Quit).\n");
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
    /*
     * GUI-subsystem binary: Windows allocates no console for us, so Explorer
     * launches stay flicker-free. For CLI flag invocations from cmd /
     * PowerShell, we attach to the parent's console so printf reaches it.
     * cmd does wait for direct child processes (regardless of subsystem),
     * so the output appears in order before the next prompt.
     */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    /* CLI flags - handled before any spawn / single-instance logic. */
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
        if (a[0] == '-') {
            fprintf(stderr, "quickrun: unknown option '%s'\n", a);
            return 2;
        }
    }

#ifdef _WIN32
    if (!already_detached()) {
        /* Launcher process. Two short-circuits:
         *   1. a daemon is already running -> silent no-op
         *   2. otherwise, spawn the detached child and exit
         *
         * When already elevated (e.g. invoked via "Run as administrator" from
         * the tray, which uses ShellExecute "runas"), we skip the Explorer
         * re-parent: Explorer runs at medium IL and re-parenting would drop
         * our integrity level. ShellExecute "runas" already produces a process
         * detached from any terminal job, so running inline is safe.
         */
        if (single_instance_exists()) {
            return 0;
        }
        if (!tray_is_elevated()) {
            if (spawn_detached_self() == 0) {
                return 0;
            }
            /* Spawn failed (very unlikely). Fall through and run inline -
             * note this instance may die with the parent if WT is the launcher. */
        }
    }

    /* Daemon path - single-instance mutex guards against a race where two
     * launchers raced through single_instance_exists() before either acquired. */
    if (!quickrun_acquire_single_instance()) {
        return 0;
    }
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
    /* No console attached - logs go to file only. */
    ql_log_init(log_path, 0);
    if (log_path) ql_log("log file: %s", log_path);
    if (err[0])   ql_log("config warning: %s", err);

    if (g_config->binding_count == 0) {
        ql_log("no bindings configured - quickrun has nothing to do. exiting.");
        quickrun_config_free(g_config);
        ql_log_close();
        return 0;
    }

    ql_log("loaded %zu binding%s",
        g_config->binding_count, g_config->binding_count == 1 ? "" : "s");

    CONFIG_LOCK_INIT();

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    hook_set_dispatch_proc(dispatch_event);

#ifdef _WIN32
    /* Tell the tray module where to find the log so left-click can tail it. */
    tray_set_log_path(log_path);

    if (tray_init() != 0) {
        ql_log("tray init failed - running hook headless");
        hook_run();
    } else {
        ql_log("daemon ready - tray icon is in the notification area");
        HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, hook_thread_proc, NULL, 0, NULL);
        if (!thread) {
            ql_log("could not start hook thread");
            tray_shutdown();
            CONFIG_LOCK();
            quickrun_config *to_free_early = g_config;
            g_config = NULL;
            CONFIG_UNLOCK();
            quickrun_config_free(to_free_early);
            ql_log_close();
            free(resolved_log_path);
            return 1;
        }
        tray_run_message_loop();
        hook_stop();
        WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
        tray_shutdown();
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
#ifdef _WIN32
    quickrun_release_single_instance();
#endif
    return 0;
}
