#ifndef TCD_CONFIG_H
#define TCD_CONFIG_H

#include <stddef.h>

#define TCD_MAX_KEYS_PER_ACTION 8

typedef struct {
    char *foreground;
    char *background;
    char *header_fg;
    char *footer_fg;
    char *path_fg;
    char *index_fg;
    char *file_fg;
    char *dir_fg;
    char *match_fg;
    char *selected_fg;
    char *selected_bg;
    char *border_fg;
    char *filter_fg;
    char *hint_fg;
    char *key_fg;
} tcd_theme;

typedef struct {
    char *items[TCD_MAX_KEYS_PER_ACTION];
    size_t count;
} tcd_keybind;

typedef struct {
    int show_index;
    int show_hidden;
    int show_size;
    int per_page;
    int wrap_navigation;

    char *sort;
    char *path_separator_display;

    tcd_theme theme;

    tcd_keybind k_up;
    tcd_keybind k_down;
    tcd_keybind k_enter;
    tcd_keybind k_back;
    tcd_keybind k_commit;
    tcd_keybind k_commit_explore;
    tcd_keybind k_cancel;
    tcd_keybind k_drives;
    tcd_keybind k_quit;
    tcd_keybind k_top;
    tcd_keybind k_bottom;
    tcd_keybind k_page_up;
    tcd_keybind k_page_down;
    tcd_keybind k_toggle_hidden;
    tcd_keybind k_clear_filter;
} tcd_config;

tcd_config *config_load(const char *override_path, char *err, size_t err_sz);
void config_free(tcd_config *c);
char *config_default_path(void);
char *config_resolve_path(const char *override_path);

size_t config_built_in_theme_count(void);
const char *config_built_in_theme_name(size_t index);

int config_key_matches(const tcd_keybind *k, const char *name, int ctrl, int alt, int shift);

#endif
