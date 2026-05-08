#include "keyspec.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uiohook.h>

typedef struct {
    const char *name;
    uint16_t    keycode;
} key_entry;

static const key_entry KEY_TABLE[] = {
    { "a", VC_A }, { "b", VC_B }, { "c", VC_C }, { "d", VC_D }, { "e", VC_E },
    { "f", VC_F }, { "g", VC_G }, { "h", VC_H }, { "i", VC_I }, { "j", VC_J },
    { "k", VC_K }, { "l", VC_L }, { "m", VC_M }, { "n", VC_N }, { "o", VC_O },
    { "p", VC_P }, { "q", VC_Q }, { "r", VC_R }, { "s", VC_S }, { "t", VC_T },
    { "u", VC_U }, { "v", VC_V }, { "w", VC_W }, { "x", VC_X }, { "y", VC_Y },
    { "z", VC_Z },

    { "0", VC_0 }, { "1", VC_1 }, { "2", VC_2 }, { "3", VC_3 }, { "4", VC_4 },
    { "5", VC_5 }, { "6", VC_6 }, { "7", VC_7 }, { "8", VC_8 }, { "9", VC_9 },

    { "f1",  VC_F1 },  { "f2",  VC_F2 },  { "f3",  VC_F3 },  { "f4",  VC_F4 },
    { "f5",  VC_F5 },  { "f6",  VC_F6 },  { "f7",  VC_F7 },  { "f8",  VC_F8 },
    { "f9",  VC_F9 },  { "f10", VC_F10 }, { "f11", VC_F11 }, { "f12", VC_F12 },
    { "f13", VC_F13 }, { "f14", VC_F14 }, { "f15", VC_F15 }, { "f16", VC_F16 },
    { "f17", VC_F17 }, { "f18", VC_F18 }, { "f19", VC_F19 }, { "f20", VC_F20 },
    { "f21", VC_F21 }, { "f22", VC_F22 }, { "f23", VC_F23 }, { "f24", VC_F24 },

    { "up",       VC_UP },        { "down",     VC_DOWN },
    { "left",     VC_LEFT },      { "right",    VC_RIGHT },
    { "home",     VC_HOME },      { "end",      VC_END },
    { "pageup",   VC_PAGE_UP },   { "pagedown", VC_PAGE_DOWN },
    { "insert",   VC_INSERT },    { "delete",   VC_DELETE },

    { "enter",    VC_ENTER },     { "return",   VC_ENTER },
    { "esc",      VC_ESCAPE },    { "escape",   VC_ESCAPE },
    { "tab",      VC_TAB },       { "space",    VC_SPACE },
    { "backspace",VC_BACKSPACE },

    { "minus",    VC_MINUS },     { "-",  VC_MINUS },
    { "equals",   VC_EQUALS },    { "=",  VC_EQUALS },
    { "semicolon",VC_SEMICOLON }, { ";",  VC_SEMICOLON },
    { "comma",    VC_COMMA },     { ",",  VC_COMMA },
    { "period",   VC_PERIOD },    { ".",  VC_PERIOD },
    { "slash",    VC_SLASH },     { "/",  VC_SLASH },
    { "quote",    VC_QUOTE },     { "'",  VC_QUOTE },
    { "backslash",VC_BACK_SLASH },{ "\\", VC_BACK_SLASH },
    { "openbracket",  VC_OPEN_BRACKET },  { "[", VC_OPEN_BRACKET },
    { "closebracket", VC_CLOSE_BRACKET }, { "]", VC_CLOSE_BRACKET },
    { "backquote", VC_BACKQUOTE }, { "`", VC_BACKQUOTE },

    { NULL, 0 }
};

static uint16_t keycode_for_name(const char *name) {
    for (const key_entry *e = KEY_TABLE; e->name; e++) {
        if (strcmp(e->name, name) == 0) return e->keycode;
    }
    return 0;
}

static int parse_modifier(const char *token, uint8_t *mods) {
    if (strcmp(token, "ctrl")    == 0 || strcmp(token, "control") == 0) { *mods |= KEYSPEC_MOD_CTRL;  return 1; }
    if (strcmp(token, "alt")     == 0 || strcmp(token, "option")  == 0) { *mods |= KEYSPEC_MOD_ALT;   return 1; }
    if (strcmp(token, "shift")   == 0)                                    { *mods |= KEYSPEC_MOD_SHIFT; return 1; }
    if (strcmp(token, "meta")    == 0 || strcmp(token, "win")     == 0
     || strcmp(token, "cmd")     == 0 || strcmp(token, "super")   == 0)  { *mods |= KEYSPEC_MOD_META;  return 1; }
    return 0;
}

int keyspec_parse(const char *spec, keyspec *out) {
    if (!spec || !out) return -1;

    memset(out, 0, sizeof(*out));
    snprintf(out->spec, sizeof(out->spec), "%s", spec);

    char buf[128];
    size_t spec_len = strlen(spec);
    if (spec_len == 0 || spec_len >= sizeof(buf)) return -1;

    for (size_t i = 0; i <= spec_len; i++) {
        buf[i] = (char)tolower((unsigned char)spec[i]);
    }

    char *token_start = buf;
    while (1) {
        char *plus = strchr(token_start, '+');
        if (plus) *plus = 0;

        if (*token_start == 0) return -1;

        if (parse_modifier(token_start, &out->modifiers)) {
            if (!plus) return -1;
            token_start = plus + 1;
            continue;
        }

        uint16_t code = keycode_for_name(token_start);
        if (code == 0) return -1;
        out->keycode = code;

        if (plus) return -1;
        return 0;
    }
}

static uint8_t modifiers_from_uiohook(uint16_t mask) {
    uint8_t mods = 0;
    if (mask & (MASK_CTRL_L  | MASK_CTRL_R))  mods |= KEYSPEC_MOD_CTRL;
    if (mask & (MASK_ALT_L   | MASK_ALT_R))   mods |= KEYSPEC_MOD_ALT;
    if (mask & (MASK_SHIFT_L | MASK_SHIFT_R)) mods |= KEYSPEC_MOD_SHIFT;
    if (mask & (MASK_META_L  | MASK_META_R))  mods |= KEYSPEC_MOD_META;
    return mods;
}

int keyspec_matches(const keyspec *spec, uint16_t keycode, uint16_t uiohook_modifier_mask) {
    if (!spec) return 0;
    if (spec->keycode != keycode) return 0;
    return modifiers_from_uiohook(uiohook_modifier_mask) == spec->modifiers;
}

const char *keyspec_keycode_name(uint16_t keycode) {
    for (const key_entry *e = KEY_TABLE; e->name; e++) {
        if (e->keycode == keycode) return e->name;
    }
    return NULL;
}
