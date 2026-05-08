#include "tray.h"

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#include <uiohook.h>

#include "log.h"

#define WM_TRAYICON           (WM_USER + 1)
#define ID_TRAY_OPEN_LOCATION 1001
#define ID_TRAY_RELOAD        1002
#define ID_TRAY_QUIT          1003

extern void quickrun_reload_config(void);

static HWND             g_tray_hwnd = NULL;
static NOTIFYICONDATAW  g_nid       = {0};

static void open_exe_location(void) {
    wchar_t exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        ql_log("tray: GetModuleFileNameW failed (err=%lu)", GetLastError());
        return;
    }

    /* `explorer /select,"<path>"` opens the containing folder with the
     * file highlighted - matches Windows' built-in "Open file location". */
    wchar_t args[2 * MAX_PATH];
    swprintf(args, sizeof(args) / sizeof(args[0]),
        L"/select,\"%ls\"", exe_path);
    ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
}

static LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TRAYICON:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
                POINT pt;
                GetCursorPos(&pt);

                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING,    ID_TRAY_OPEN_LOCATION, L"Open file location");
                AppendMenuW(menu, MF_STRING,    ID_TRAY_RELOAD,        L"Reload config");
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
                    open_exe_location();
                    break;
                case ID_TRAY_RELOAD:
                    ql_log("tray: reload requested");
                    quickrun_reload_config();
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
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int tray_init(void) {
    HINSTANCE hInst = GetModuleHandleW(NULL);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = tray_wndproc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"QuickrunTrayWnd";
    if (!RegisterClassW(&wc)) {
        ql_log("tray: RegisterClassW failed (err=%lu)", GetLastError());
        return -1;
    }

    /*
     * A normal top-level window (not HWND_MESSAGE) — message-only windows
     * sometimes get filtered out by Shell_NotifyIcon on Windows 10/11.
     * We never call ShowWindow, so it stays invisible regardless.
     */
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

    /* Load the embedded icon resource (id=1, set in build script's .rc file).
     * Falls back to the generic IDI_APPLICATION if the resource is missing. */
    g_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!g_nid.hIcon) {
        g_nid.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512)); /* IDI_APPLICATION */
        ql_log("tray: app icon resource not found, using default");
    }
    wcsncpy(g_nid.szTip, L"quickrun", sizeof(g_nid.szTip) / sizeof(wchar_t) - 1);

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        ql_log("tray: Shell_NotifyIcon NIM_ADD failed (err=%lu)", GetLastError());
        DestroyWindow(g_tray_hwnd);
        g_tray_hwnd = NULL;
        return -1;
    }

    /* Opt in to the modern NotifyIcon version so right-click delivers
     * WM_CONTEXTMENU and the icon plays nicely with Win10/11 explorer. */
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
}

void tray_run_message_loop(void) {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

#endif /* _WIN32 */
