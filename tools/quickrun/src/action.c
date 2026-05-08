#include "action.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

void action_free(action *a) {
    if (!a) return;
    free(a->command);
    if (a->args) {
        for (size_t i = 0; i < a->arg_count; i++) free(a->args[i]);
        free(a->args);
    }
    free(a->target);
    memset(a, 0, sizeof(*a));
}

#ifdef _WIN32
static int execute_run_windows(const action *a) {
    size_t cmdline_cap = 4096;
    char *cmdline = (char *)malloc(cmdline_cap);
    if (!cmdline) return -1;

    int needs_quote = (strchr(a->command, ' ') != NULL);
    int written = snprintf(cmdline, cmdline_cap, "%s%s%s",
        needs_quote ? "\"" : "", a->command, needs_quote ? "\"" : "");
    if (written < 0 || (size_t)written >= cmdline_cap) { free(cmdline); return -1; }

    for (size_t i = 0; i < a->arg_count; i++) {
        size_t remaining = cmdline_cap - (size_t)written;
        int    arg_quote = (strchr(a->args[i], ' ') != NULL);
        int    n = snprintf(cmdline + written, remaining, " %s%s%s",
            arg_quote ? "\"" : "", a->args[i], arg_quote ? "\"" : "");
        if (n < 0 || (size_t)n >= remaining) { free(cmdline); return -1; }
        written += n;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi);
    free(cmdline);
    if (!ok) {
        ql_log("action: CreateProcess failed for '%s' (err=%lu)", a->command, GetLastError());
        return -1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

static int execute_open_windows(const action *a) {
    HINSTANCE r = ShellExecuteA(NULL, "open", a->target, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        ql_log("action: ShellExecute failed for '%s' (rc=%lld)", a->target, (long long)(INT_PTR)r);
        return -1;
    }
    return 0;
}
#else
static int execute_run_posix(const action *a) {
    pid_t pid = fork();
    if (pid < 0) {
        ql_log("action: fork failed");
        return -1;
    }
    if (pid == 0) {
        size_t total = a->arg_count + 2;
        char **argv = (char **)calloc(total, sizeof(char *));
        if (!argv) _exit(127);
        argv[0] = a->command;
        for (size_t i = 0; i < a->arg_count; i++) argv[i + 1] = a->args[i];
        argv[total - 1] = NULL;

        execvp(a->command, argv);
        _exit(127);
    }
    return 0;
}

static int execute_open_posix(const action *a) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
#  ifdef __APPLE__
        execlp("open", "open", a->target, (char *)NULL);
#  else
        execlp("xdg-open", "xdg-open", a->target, (char *)NULL);
#  endif
        _exit(127);
    }
    return 0;
}
#endif

int action_execute(const action *a) {
    if (!a) return -1;

    switch (a->kind) {
        case ACTION_RUN:
            if (!a->command || !*a->command) {
                ql_log("action: 'run' missing command");
                return -1;
            }
            ql_log("action: run '%s'", a->command);
#ifdef _WIN32
            return execute_run_windows(a);
#else
            return execute_run_posix(a);
#endif

        case ACTION_OPEN:
            if (!a->target || !*a->target) {
                ql_log("action: 'open' missing target");
                return -1;
            }
            ql_log("action: open '%s'", a->target);
#ifdef _WIN32
            return execute_open_windows(a);
#else
            return execute_open_posix(a);
#endif
    }
    return -1;
}
