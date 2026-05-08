#ifndef QUICKRUN_KEYSPEC_H
#define QUICKRUN_KEYSPEC_H

#include <stdint.h>

#define KEYSPEC_MOD_CTRL  0x01
#define KEYSPEC_MOD_ALT   0x02
#define KEYSPEC_MOD_SHIFT 0x04
#define KEYSPEC_MOD_META  0x08

typedef struct {
    uint16_t keycode;       /* libuiohook VC_* keycode; 0 = invalid */
    uint8_t  modifiers;     /* OR of KEYSPEC_MOD_* */
    char     spec[64];      /* original spec string, for logging */
} keyspec;

int  keyspec_parse(const char *spec, keyspec *out);
int  keyspec_matches(const keyspec *spec, uint16_t keycode, uint16_t uiohook_modifier_mask);
const char *keyspec_keycode_name(uint16_t keycode);

#endif
