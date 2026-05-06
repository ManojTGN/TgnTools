#include "term.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#else
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/ioctl.h>
#  include <termios.h>
#  include <unistd.h>
#endif

#define BUF_CAP 8192

static char g_buf[BUF_CAP];
static size_t g_buf_len = 0;

#ifdef _WIN32
static HANDLE g_in = INVALID_HANDLE_VALUE;
static HANDLE g_out = INVALID_HANDLE_VALUE;
static DWORD g_orig_in_mode = 0;
static DWORD g_orig_out_mode = 0;
static UINT g_orig_cp = 0;
static UINT g_orig_output_cp = 0;
#else
static int g_tty_in = -1;
static int g_tty_out = -1;
static struct termios g_orig_termios;
static int g_termios_saved = 0;
#endif

static int g_alt_screen = 0;
static int g_initialized = 0;

#ifdef _WIN32
HANDLE term_handle_in(void) { return g_in; }
HANDLE term_handle_out(void) { return g_out; }
#else
int term_fd_in(void) { return g_tty_in; }
int term_fd_out(void) { return g_tty_out; }
#endif

static void term_raw_flush(void) {
    if (g_buf_len == 0) return;
#ifdef _WIN32
    DWORD written;
    WriteFile(g_out, g_buf, (DWORD)g_buf_len, &written, NULL);
#else
    ssize_t off = 0;
    while ((size_t)off < g_buf_len) {
        ssize_t w = write(g_tty_out, g_buf + off, g_buf_len - off);
        if (w <= 0) break;
        off += w;
    }
#endif
    g_buf_len = 0;
}

void term_flush(void) { term_raw_flush(); }

void term_write_n(const char *s, size_t n) {
    if (n == 0) return;
    if (g_buf_len + n > BUF_CAP) term_raw_flush();
    if (n > BUF_CAP) {
#ifdef _WIN32
        DWORD written;
        WriteFile(g_out, s, (DWORD)n, &written, NULL);
#else
        ssize_t off = 0;
        while ((size_t)off < n) {
            ssize_t w = write(g_tty_out, s + off, n - off);
            if (w <= 0) break;
            off += w;
        }
#endif
        return;
    }
    memcpy(g_buf + g_buf_len, s, n);
    g_buf_len += n;
}

void term_write(const char *s) {
    if (!s) return;
    term_write_n(s, strlen(s));
}

void term_writef(const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n > sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    term_write_n(tmp, (size_t)n);
}

void term_clear(void)        { term_write("\x1b[2J\x1b[H"); }
void term_clear_line(void)   { term_write("\x1b[2K"); }
void term_hide_cursor(void)  { term_write("\x1b[?25l"); }
void term_show_cursor(void)  { term_write("\x1b[?25h"); }
void term_alt_screen_on(void){ term_write("\x1b[?1049h"); g_alt_screen = 1; }
void term_alt_screen_off(void){ if (g_alt_screen) { term_write("\x1b[?1049l"); g_alt_screen = 0; } }
void term_reset_attrs(void)  { term_write("\x1b[0m"); }
void term_set_bold(int on)   { term_write(on ? "\x1b[1m" : "\x1b[22m"); }
void term_set_inverse(int on){ term_write(on ? "\x1b[7m" : "\x1b[27m"); }

void term_move(int row, int col) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
    if (n > 0) term_write_n(buf, (size_t)n);
}

static int parse_named_color(const char *name, int bg, char *out, size_t out_sz) {
    if (!name) return 0;
    int base = bg ? 40 : 30;
    int bright_base = bg ? 100 : 90;
    int code = -1, bright = 0;

    if (strncmp(name, "bright_", 7) == 0) { bright = 1; name += 7; }
    else if (strncmp(name, "bright-", 7) == 0) { bright = 1; name += 7; }

    if      (strcmp(name, "black")   == 0) code = 0;
    else if (strcmp(name, "red")     == 0) code = 1;
    else if (strcmp(name, "green")   == 0) code = 2;
    else if (strcmp(name, "yellow")  == 0) code = 3;
    else if (strcmp(name, "blue")    == 0) code = 4;
    else if (strcmp(name, "magenta") == 0) code = 5;
    else if (strcmp(name, "cyan")    == 0) code = 6;
    else if (strcmp(name, "white")   == 0) code = 7;
    else if (strcmp(name, "gray")    == 0 || strcmp(name, "grey") == 0) {
        bright = 1; code = 0;
    } else if (strcmp(name, "default") == 0 || strcmp(name, "") == 0) {
        snprintf(out, out_sz, "\x1b[%dm", bg ? 49 : 39);
        return 1;
    }
    if (code < 0) return 0;
    snprintf(out, out_sz, "\x1b[%dm", (bright ? bright_base : base) + code);
    return 1;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static int parse_hex_color(const char *s, int bg, char *out, size_t out_sz) {
    if (!s || s[0] != '#') return 0;
    size_t l = strlen(s);
    int r, g, b;
    if (l == 7) {
        int hh[6];
        for (int i = 0; i < 6; i++) {
            hh[i] = hex_digit(s[1 + i]);
            if (hh[i] < 0) return 0;
        }
        r = (hh[0] << 4) | hh[1];
        g = (hh[2] << 4) | hh[3];
        b = (hh[4] << 4) | hh[5];
    } else if (l == 4) {
        int hh[3];
        for (int i = 0; i < 3; i++) {
            hh[i] = hex_digit(s[1 + i]);
            if (hh[i] < 0) return 0;
        }
        r = hh[0] * 17; g = hh[1] * 17; b = hh[2] * 17;
    } else return 0;
    snprintf(out, out_sz, "\x1b[%d;2;%d;%d;%dm", bg ? 48 : 38, r, g, b);
    return 1;
}

static void apply_color(const char *spec, int bg) {
    if (!spec || !*spec) return;
    char buf[64];
    if (parse_hex_color(spec, bg, buf, sizeof(buf))) { term_write(buf); return; }
    if (parse_named_color(spec, bg, buf, sizeof(buf))) { term_write(buf); return; }
}

void term_set_fg(const char *color) { apply_color(color, 0); }
void term_set_bg(const char *color) { apply_color(color, 1); }

void term_get_size(int *cols, int *rows) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (g_out != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(g_out, &info)) {
        *cols = info.srWindow.Right - info.srWindow.Left + 1;
        *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        if (*cols < 20) *cols = 80;
        if (*rows < 5) *rows = 24;
        return;
    }
#else
    struct winsize ws;
    if (g_tty_out >= 0 && ioctl(g_tty_out, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return;
    }
#endif
    *cols = 80;
    *rows = 24;
}

static void shutdown_atexit(void) { term_shutdown(); }

#ifndef _WIN32
static void on_signal(int sig) {
    (void)sig;
    term_shutdown();
    _exit(130);
}
#endif

void term_init(void) {
    if (g_initialized) return;
#ifdef _WIN32
    g_in  = CreateFileA("CONIN$",  GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,  NULL, OPEN_EXISTING, 0, NULL);
    g_out = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_in == INVALID_HANDLE_VALUE || g_out == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "tcd: cannot open console\n");
        exit(1);
    }
    GetConsoleMode(g_in, &g_orig_in_mode);
    GetConsoleMode(g_out, &g_orig_out_mode);
    DWORD in_mode = g_orig_in_mode;
    in_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_MOUSE_INPUT);
    in_mode |= ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT;
    SetConsoleMode(g_in, in_mode);
    DWORD out_mode = g_orig_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(g_out, out_mode);
    g_orig_cp = GetConsoleCP();
    g_orig_output_cp = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#else
    g_tty_in = open("/dev/tty", O_RDONLY);
    g_tty_out = open("/dev/tty", O_WRONLY);
    if (g_tty_in < 0 || g_tty_out < 0) {
        fprintf(stderr, "tcd: cannot open /dev/tty\n");
        exit(1);
    }
    if (tcgetattr(g_tty_in, &g_orig_termios) != 0) {
        fprintf(stderr, "tcd: not a tty\n");
        exit(1);
    }
    g_termios_saved = 1;
    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(g_tty_in, TCSAFLUSH, &raw);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
#endif
    atexit(shutdown_atexit);
    term_alt_screen_on();
    term_hide_cursor();
    term_flush();
    g_initialized = 1;
}

void term_shutdown(void) {
    if (!g_initialized) return;
    term_show_cursor();
    term_reset_attrs();
    term_alt_screen_off();
    term_flush();
#ifdef _WIN32
    if (g_orig_in_mode)  SetConsoleMode(g_in, g_orig_in_mode);
    if (g_orig_out_mode) SetConsoleMode(g_out, g_orig_out_mode);
    if (g_orig_cp)        SetConsoleCP(g_orig_cp);
    if (g_orig_output_cp) SetConsoleOutputCP(g_orig_output_cp);
    if (g_in  != INVALID_HANDLE_VALUE) { CloseHandle(g_in);  g_in  = INVALID_HANDLE_VALUE; }
    if (g_out != INVALID_HANDLE_VALUE) { CloseHandle(g_out); g_out = INVALID_HANDLE_VALUE; }
#else
    if (g_termios_saved && g_tty_in >= 0) tcsetattr(g_tty_in, TCSAFLUSH, &g_orig_termios);
    if (g_tty_in  >= 0) { close(g_tty_in);  g_tty_in  = -1; }
    if (g_tty_out >= 0) { close(g_tty_out); g_tty_out = -1; }
#endif
    g_initialized = 0;
}
