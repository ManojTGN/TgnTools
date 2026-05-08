#include "tray.h"

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

#include <uiohook.h>

#include "log.h"

#define WM_TRAYICON           (WM_USER + 1)
#define ID_TRAY_OPEN_LOCATION 1001
#define ID_TRAY_RELOAD        1002
#define ID_TRAY_ELEVATE       1003
#define ID_TRAY_QUIT          1004

#ifndef NIN_SELECT
#  define NIN_SELECT (WM_USER + 0)
#endif

extern void quickrun_reload_config(void);
extern void quickrun_release_single_instance(void);
extern int  quickrun_acquire_single_instance(void);

int tray_is_elevated(void) {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;

    TOKEN_ELEVATION elevation = { 0 };
    DWORD size = sizeof(elevation);
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

static void tray_elevate(HWND hwnd) {
    wchar_t exe_path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, exe_path,
        sizeof(exe_path) / sizeof(exe_path[0]));
    if (n == 0 || n >= sizeof(exe_path) / sizeof(exe_path[0])) {
        ql_log("tray: GetModuleFileNameW failed for elevation");
        return;
    }

    /* Release the single-instance mutex first so the elevated copy can
     * acquire it once UAC consents. If the user cancels UAC, we re-acquire
     * (or quit if someone else grabbed the slot in the meantime). */
    quickrun_release_single_instance();

    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof(sei);
    sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path;
    sei.nShow  = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        ql_log("tray: elevation %s (err=%lu) - resuming without elevation",
            err == ERROR_CANCELLED ? "cancelled" : "failed", err);
        if (!quickrun_acquire_single_instance()) {
            ql_log("tray: another instance grabbed the slot - quitting");
            hook_stop();
            DestroyWindow(hwnd);
        }
        return;
    }

    if (sei.hProcess) CloseHandle(sei.hProcess);
    ql_log("tray: elevated instance launched - exiting non-elevated");
    hook_stop();
    DestroyWindow(hwnd);
}

static HWND             g_tray_hwnd        = NULL;
static NOTIFYICONDATAW  g_nid              = { 0 };
static wchar_t          g_log_path_w[1024] = { 0 };
static HANDLE           g_log_console_proc = NULL;

void tray_set_log_path(const char *utf8_path) {
    if (!utf8_path || !*utf8_path) {
        g_log_path_w[0] = 0;
        return;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1,
        g_log_path_w, sizeof(g_log_path_w) / sizeof(g_log_path_w[0]));
    if (n <= 0) g_log_path_w[0] = 0;
}

typedef struct {
    DWORD pid;
    HWND  result;
} pid_window_lookup;

static BOOL CALLBACK find_main_window_for_pid_proc(HWND hwnd, LPARAM lp) {
    pid_window_lookup *l = (pid_window_lookup *)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == l->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == NULL) {
        l->result = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND find_main_window_for_pid(DWORD pid) {
    pid_window_lookup l = { pid, NULL };
    EnumWindows(find_main_window_for_pid_proc, (LPARAM)&l);
    return l.result;
}

static int log_console_alive(void) {
    if (!g_log_console_proc) return 0;
    DWORD code = 0;
    if (!GetExitCodeProcess(g_log_console_proc, &code)) return 0;
    return code == STILL_ACTIVE;
}

static void focus_existing_log_console(void) {
    DWORD pid = GetProcessId(g_log_console_proc);
    HWND  wnd = find_main_window_for_pid(pid);
    if (!wnd) return;
    if (IsIconic(wnd)) ShowWindow(wnd, SW_RESTORE);
    SetForegroundWindow(wnd);
}

/*
 * Spawn a separate console process that tails the log file with PowerShell's
 * `Get-Content -Wait`. Closing that window only kills PowerShell - the
 * quickrun daemon keeps running.
 */
static void tray_show_log_console(void) {
    if (log_console_alive()) {
        focus_existing_log_console();
        return;
    }
    if (g_log_console_proc) {
        CloseHandle(g_log_console_proc);
        g_log_console_proc = NULL;
    }

    if (!g_log_path_w[0]) {
        ql_log("tray: log path not set, can't open console");
        return;
    }

    /* Escape single quotes in the path for the PowerShell single-quoted string. */
    wchar_t escaped[1024 * 2];
    {
        const wchar_t *src = g_log_path_w;
        wchar_t       *dst = escaped;
        size_t cap = sizeof(escaped) / sizeof(escaped[0]);
        size_t i = 0;
        while (*src && i + 2 < cap) {
            if (*src == L'\'') { dst[i++] = L'\''; }
            dst[i++] = *src++;
        }
        dst[i] = 0;
    }

    wchar_t cmdline[2048];
    swprintf(cmdline, sizeof(cmdline) / sizeof(cmdline[0]),
        L"powershell.exe -NoProfile -NoExit -Command "
        L"\"$Host.UI.RawUI.WindowTitle = 'quickrun log'; "
        L"if (-not (Test-Path -LiteralPath '%ls')) { "
        L"  New-Item -ItemType File -Path '%ls' -Force | Out-Null "
        L"}; "
        L"Get-Content -Wait -LiteralPath '%ls'\"",
        escaped, escaped, escaped);

    STARTUPINFOW       si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);

    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
            CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
    {
        ql_log("tray: failed to spawn log console (err=%lu)", GetLastError());
        return;
    }

    g_log_console_proc = pi.hProcess;
    CloseHandle(pi.hThread);
    ql_log("tray: log console opened (pid %lu)", pi.dwProcessId);
}

static void tray_open_exe_location(void) {
    wchar_t exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        ql_log("tray: GetModuleFileNameW failed (err=%lu)", GetLastError());
        return;
    }

    wchar_t args[2 * MAX_PATH];
    swprintf(args, sizeof(args) / sizeof(args[0]), L"/select,\"%ls\"", exe_path);
    ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
}

static LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TRAYICON:
            if (LOWORD(lp) == WM_LBUTTONUP
             || LOWORD(lp) == WM_LBUTTONDBLCLK
             || LOWORD(lp) == NIN_SELECT)
            {
                tray_show_log_console();
                return 0;
            }
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
                POINT pt;
                GetCursorPos(&pt);

                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING,    ID_TRAY_OPEN_LOCATION, L"Open file location");
                AppendMenuW(menu, MF_STRING,    ID_TRAY_RELOAD,        L"Reload config");
                AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                if (tray_is_elevated()) {
                    AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_TRAY_ELEVATE,
                        L"Running in privileged mode");
                } else {
                    AppendMenuW(menu, MF_STRING, ID_TRAY_ELEVATE,
                        L"Run as administrator...");
                }
                AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(menu, MF_STRING,    ID_TRAY_QUIT,          L"Quit quickrun");

                SetForegroundWindow(hwnd);
                TrackPopupMenu(menu,
                    TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                    pt.x, pt.y, 0, hwnd, NULL);
                PostMessageW(hwnd, WM_NULL, 0, 0);
                DestroyMenu(menu);
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_TRAY_OPEN_LOCATION:
                    ql_log("tray: open file location");
                    tray_open_exe_location();
                    break;
                case ID_TRAY_RELOAD:
                    ql_log("tray: reload requested");
                    quickrun_reload_config();
                    break;
                case ID_TRAY_ELEVATE:
                    ql_log("tray: elevation requested");
                    tray_elevate(hwnd);
                    break;
                case ID_TRAY_QUIT:
                    ql_log("tray: quit requested");
                    hook_stop();
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;

        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_log_console_proc) {
                CloseHandle(g_log_console_proc);
                g_log_console_proc = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int tray_init(void) {
    HINSTANCE hInst = GetModuleHandleW(NULL);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc   = tray_wndproc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"QuickrunTrayWnd";
    if (!RegisterClassW(&wc)) {
        ql_log("tray: RegisterClassW failed (err=%lu)", GetLastError());
        return -1;
    }

    g_tray_hwnd = CreateWindowExW(0,
        L"QuickrunTrayWnd", L"quickrun",
        WS_OVERLAPPED, 0, 0, 0, 0,
        NULL, NULL, hInst, NULL);
    if (!g_tray_hwnd) {
        ql_log("tray: CreateWindowExW failed (err=%lu)", GetLastError());
        return -1;
    }

    g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd             = g_tray_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;

    g_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!g_nid.hIcon) {
        g_nid.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
        ql_log("tray: app icon resource not found, using default");
    }
    wcsncpy(g_nid.szTip, L"quickrun", sizeof(g_nid.szTip) / sizeof(wchar_t) - 1);

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        ql_log("tray: Shell_NotifyIcon NIM_ADD failed (err=%lu)", GetLastError());
        DestroyWindow(g_tray_hwnd);
        g_tray_hwnd = NULL;
        return -1;
    }

    g_nid.uVersion = 4 /* NOTIFYICON_VERSION_4 */;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &g_nid)) {
        ql_log("tray: NIM_SETVERSION failed (err=%lu) - non-fatal", GetLastError());
    }

    ql_log("tray: icon registered (hwnd=%p)", (void *)g_tray_hwnd);
    return 0;
}

void tray_shutdown(void) {
    if (g_tray_hwnd) {
        DestroyWindow(g_tray_hwnd);
        g_tray_hwnd = NULL;
    }
    if (g_log_console_proc) {
        CloseHandle(g_log_console_proc);
        g_log_console_proc = NULL;
    }
}

void tray_run_message_loop(void) {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

#endif /* _WIN32 */
