#include "input.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
extern HANDLE term_handle_in(void);
#else
#  include <sys/select.h>
#  include <unistd.h>
extern int term_fd_in(void);
#endif

static key_event ke(key_type t) {
    key_event k = {0};
    k.type = t;
    return k;
}

#ifndef _WIN32
static int read_byte_timeout(int ms, unsigned char *out) {
    int fd = term_fd_in();
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    int r = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) return 0;
    return read(fd, out, 1) == 1;
}

static int read_byte_block(unsigned char *out) {
    int fd = term_fd_in();
    return read(fd, out, 1) == 1;
}

static key_event posix_read(void) {
    unsigned char b;
    if (!read_byte_block(&b)) return ke(KEY_NONE);

    if (b == 0x1b) {
        unsigned char b2;
        if (!read_byte_timeout(50, &b2)) return ke(KEY_ESC);
        if (b2 == '[' || b2 == 'O') {
            unsigned char seq[16];
            int i = 0;
            while (i < 15) {
                unsigned char nb;
                if (!read_byte_timeout(50, &nb)) break;
                seq[i++] = nb;
                if (nb >= 0x40 && nb <= 0x7e) break;
            }
            seq[i] = 0;
            if (i == 1) {
                switch (seq[0]) {
                    case 'A': return ke(KEY_UP);
                    case 'B': return ke(KEY_DOWN);
                    case 'C': return ke(KEY_RIGHT);
                    case 'D': return ke(KEY_LEFT);
                    case 'H': return ke(KEY_HOME);
                    case 'F': return ke(KEY_END);
                    case 'Z': return ke(KEY_SHIFT_TAB);
                    case 'P': return ke(KEY_F1);
                    case 'Q': return ke(KEY_F2);
                    case 'R': return ke(KEY_F3);
                    case 'S': return ke(KEY_F4);
                }
            }
            if (i >= 2 && seq[i-1] == '~') {
                int n = 0;
                for (int j = 0; j < i - 1; j++) {
                    if (seq[j] < '0' || seq[j] > '9') { n = -1; break; }
                    n = n * 10 + (seq[j] - '0');
                }
                switch (n) {
                    case 1: case 7: return ke(KEY_HOME);
                    case 4: case 8: return ke(KEY_END);
                    case 2: { key_event k = ke(KEY_NONE); return k; }
                    case 3: return ke(KEY_DELETE);
                    case 5: return ke(KEY_PAGE_UP);
                    case 6: return ke(KEY_PAGE_DOWN);
                    case 15: return ke(KEY_F5);
                    case 17: return ke(KEY_F6);
                    case 18: return ke(KEY_F7);
                    case 19: return ke(KEY_F8);
                    case 20: return ke(KEY_F9);
                    case 21: return ke(KEY_F10);
                    case 23: return ke(KEY_F11);
                    case 24: return ke(KEY_F12);
                }
            }
            return ke(KEY_NONE);
        }
        key_event k = ke(KEY_CHAR);
        k.ch = b2;
        k.alt = 1;
        return k;
    }

    if (b == '\r' || b == '\n') return ke(KEY_ENTER);
    if (b == '\t') return ke(KEY_TAB);
    if (b == 0x7f || b == 0x08) return ke(KEY_BACKSPACE);
    if (b == 0x03) return ke(KEY_INTERRUPT);

    if (b < 0x20) {
        key_event k = ke(KEY_CHAR);
        k.ch = b + 'a' - 1;
        k.ctrl = 1;
        return k;
    }

    if (b < 0x80) {
        key_event k = ke(KEY_CHAR);
        k.ch = b;
        return k;
    }

    int extra = 0;
    uint32_t cp = 0;
    if ((b & 0xe0) == 0xc0)      { cp = b & 0x1f; extra = 1; }
    else if ((b & 0xf0) == 0xe0) { cp = b & 0x0f; extra = 2; }
    else if ((b & 0xf8) == 0xf0) { cp = b & 0x07; extra = 3; }
    else                         { return ke(KEY_NONE); }
    for (int i = 0; i < extra; i++) {
        unsigned char nb;
        if (!read_byte_timeout(20, &nb)) return ke(KEY_NONE);
        if ((nb & 0xc0) != 0x80) return ke(KEY_NONE);
        cp = (cp << 6) | (nb & 0x3f);
    }
    key_event k = ke(KEY_CHAR);
    k.ch = cp;
    return k;
}
#endif

#ifdef _WIN32
static key_event win_read(void) {
    HANDLE h = term_handle_in();
    INPUT_RECORD rec;
    DWORD got;
    while (1) {
        if (!ReadConsoleInputW(h, &rec, 1, &got) || got == 0) return ke(KEY_NONE);
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) return ke(KEY_RESIZE);
        if (rec.EventType != KEY_EVENT) continue;
        if (!rec.Event.KeyEvent.bKeyDown) continue;

        WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
        DWORD st = rec.Event.KeyEvent.dwControlKeyState;
        wchar_t wc = rec.Event.KeyEvent.uChar.UnicodeChar;
        int ctrl = (st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) ? 1 : 0;
        int alt  = (st & (LEFT_ALT_PRESSED  | RIGHT_ALT_PRESSED))  ? 1 : 0;
        int shift = (st & SHIFT_PRESSED) ? 1 : 0;

        switch (vk) {
            case VK_UP:    return ke(KEY_UP);
            case VK_DOWN:  return ke(KEY_DOWN);
            case VK_LEFT:  return ke(KEY_LEFT);
            case VK_RIGHT: return ke(KEY_RIGHT);
            case VK_HOME:  return ke(KEY_HOME);
            case VK_END:   return ke(KEY_END);
            case VK_PRIOR: return ke(KEY_PAGE_UP);
            case VK_NEXT:  return ke(KEY_PAGE_DOWN);
            case VK_DELETE: return ke(KEY_DELETE);
            case VK_RETURN: {
                key_event ek = ke(KEY_ENTER);
                ek.ctrl = ctrl;
                ek.shift = shift;
                ek.alt = alt;
                return ek;
            }
            case VK_ESCAPE: return ke(KEY_ESC);
            case VK_BACK:   return ke(KEY_BACKSPACE);
            case VK_TAB:    return shift ? ke(KEY_SHIFT_TAB) : ke(KEY_TAB);
            case VK_F1: return ke(KEY_F1);
            case VK_F2: return ke(KEY_F2);
            case VK_F3: return ke(KEY_F3);
            case VK_F4: return ke(KEY_F4);
            case VK_F5: return ke(KEY_F5);
            case VK_F6: return ke(KEY_F6);
            case VK_F7: return ke(KEY_F7);
            case VK_F8: return ke(KEY_F8);
            case VK_F9: return ke(KEY_F9);
            case VK_F10: return ke(KEY_F10);
            case VK_F11: return ke(KEY_F11);
            case VK_F12: return ke(KEY_F12);
        }

        if (ctrl && wc == 3) return ke(KEY_INTERRUPT);

        if (wc != 0) {
            key_event k = ke(KEY_CHAR);
            k.ch = (uint32_t)wc;
            k.ctrl = ctrl;
            k.alt = alt;
            k.shift = shift;
            if (ctrl && wc < 0x20) k.ch = wc + 'a' - 1;
            return k;
        }
    }
}
#endif

key_event input_read(void) {
#ifdef _WIN32
    return win_read();
#else
    return posix_read();
#endif
}
