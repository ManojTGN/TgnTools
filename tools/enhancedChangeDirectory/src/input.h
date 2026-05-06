#ifndef TCD_INPUT_H
#define TCD_INPUT_H

#include <stdint.h>

typedef enum {
    KEY_NONE = 0,
    KEY_CHAR,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_ENTER,
    KEY_ESC,
    KEY_TAB,
    KEY_SHIFT_TAB,
    KEY_BACKSPACE,
    KEY_DELETE,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_RESIZE,
    KEY_INTERRUPT
} key_type;

typedef struct {
    key_type type;
    uint32_t ch;
    int ctrl;
    int alt;
    int shift;
} key_event;

key_event input_read(void);
const char *input_action_for_key(const key_event *k);

#endif
