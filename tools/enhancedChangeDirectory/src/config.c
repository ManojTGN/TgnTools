#include "config.h"
#include "json.h"
#include "fs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  include <shlobj.h>
#  define strncasecmp _strnicmp
#else
#  include <strings.h>
#endif

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

static void set_str(char **dst, const char *src) {
    free(*dst);
    *dst = xstrdup(src);
}

static void keybind_clear(tcd_keybind *k) {
    for (size_t i = 0; i < k->count; i++) free(k->items[i]);
    k->count = 0;
}

static void keybind_set(tcd_keybind *k, const char *const *names, size_t n) {
    keybind_clear(k);
    for (size_t i = 0; i < n && i < TCD_MAX_KEYS_PER_ACTION; i++) {
        k->items[k->count++] = xstrdup(names[i]);
    }
}

static void keybind_set1(tcd_keybind *k, const char *a) {
    const char *arr[1] = { a };
    keybind_set(k, arr, 1);
}

static void keybind_set2(tcd_keybind *k, const char *a, const char *b) {
    const char *arr[2] = { a, b };
    keybind_set(k, arr, 2);
}

static void apply_defaults(tcd_config *c) {
    c->show_index = 1;
    c->show_hidden = 0;
    c->show_size = 0;
    c->per_page = 20;
    c->wrap_navigation = 0;
    set_str(&c->sort, "dirs_first");
    set_str(&c->path_separator_display, NULL);

    set_str(&c->theme.foreground,  "default");
    set_str(&c->theme.background,  "default");
    set_str(&c->theme.header_fg,   "bright_cyan");
    set_str(&c->theme.footer_fg,   "gray");
    set_str(&c->theme.path_fg,     "bright_white");
    set_str(&c->theme.index_fg,    "gray");
    set_str(&c->theme.file_fg,     "default");
    set_str(&c->theme.dir_fg,      "bright_blue");
    set_str(&c->theme.match_fg,    "bright_yellow");
    set_str(&c->theme.selected_fg, "black");
    set_str(&c->theme.selected_bg, "bright_cyan");
    set_str(&c->theme.border_fg,   "gray");
    set_str(&c->theme.filter_fg,   "bright_yellow");
    set_str(&c->theme.hint_fg,     "gray");
    set_str(&c->theme.key_fg,      "bright_yellow");

    keybind_set1(&c->k_up,     "up");
    keybind_set1(&c->k_down,   "down");
    keybind_set2(&c->k_enter,  "enter", "right");
    keybind_set1(&c->k_back,   "left");
    keybind_set1(&c->k_commit, "ctrl+enter");
    keybind_set1(&c->k_commit_explore, "ctrl+shift+enter");
    keybind_set1(&c->k_cancel, "esc");
    keybind_set1(&c->k_drives, "tab");
    keybind_set1(&c->k_quit,   "ctrl+c");
    keybind_set1(&c->k_top,    "home");
    keybind_set1(&c->k_bottom, "end");
    keybind_set1(&c->k_page_up,   "pageup");
    keybind_set1(&c->k_page_down, "pagedown");
    keybind_set1(&c->k_toggle_hidden, "ctrl+h");
    keybind_set1(&c->k_clear_filter, "ctrl+u");
    keybind_set1(&c->k_save_bookmark, "ctrl+s");
}

static void apply_keybind_from_json(tcd_keybind *kb, json_value *node) {
    if (!node) return;
    if (node->type == JSON_STRING) {
        keybind_set1(kb, node->v.string);
        return;
    }
    if (node->type == JSON_ARRAY) {
        keybind_clear(kb);
        size_t lim = node->v.array.count;
        if (lim > TCD_MAX_KEYS_PER_ACTION) lim = TCD_MAX_KEYS_PER_ACTION;
        for (size_t i = 0; i < lim; i++) {
            json_value *it = node->v.array.items[i];
            if (it && it->type == JSON_STRING) kb->items[kb->count++] = xstrdup(it->v.string);
        }
    }
}

static void apply_str_from_json(char **dst, json_value *root, const char *key) {
    json_value *v = json_get(root, key);
    const char *s = json_string(v);
    if (s) set_str(dst, s);
}

typedef struct {
    const char *name;
    const char *foreground, *background;
    const char *header_fg, *footer_fg, *path_fg;
    const char *index_fg, *file_fg, *dir_fg, *match_fg;
    const char *selected_fg, *selected_bg;
    const char *border_fg, *filter_fg, *hint_fg, *key_fg;
} theme_preset;

static const theme_preset PRESETS[] = {
    {
        "default",
        "default",  "default",
        "bright_cyan", "gray",       "bright_white",
        "gray",        "default",    "bright_blue",   "bright_yellow",
        "black",       "bright_cyan",
        "gray",        "bright_yellow", "gray",       "bright_yellow",
    },
    {
        "dracula",
        "#f8f8f2", "#282a36",
        "#bd93f9", "#6272a4", "#f8f8f2",
        "#6272a4", "#f8f8f2", "#8be9fd", "#ffb86c",
        "#282a36", "#bd93f9",
        "#44475a", "#ff79c6", "#6272a4", "#50fa7b",
    },
    {
        "nord",
        "#d8dee9", "#2e3440",
        "#88c0d0", "#4c566a", "#eceff4",
        "#4c566a", "#d8dee9", "#81a1c1", "#ebcb8b",
        "#2e3440", "#88c0d0",
        "#434c5e", "#ebcb8b", "#4c566a", "#a3be8c",
    },
    {
        "solarized-dark",
        "#93a1a1", "#002b36",
        "#268bd2", "#586e75", "#eee8d5",
        "#586e75", "#93a1a1", "#2aa198", "#b58900",
        "#002b36", "#268bd2",
        "#073642", "#cb4b16", "#586e75", "#859900",
    },
    {
        "gruvbox-dark",
        "#ebdbb2", "#282828",
        "#fabd2f", "#7c6f64", "#ebdbb2",
        "#7c6f64", "#ebdbb2", "#83a598", "#fe8019",
        "#282828", "#fabd2f",
        "#504945", "#fe8019", "#7c6f64", "#b8bb26",
    },
    {
        "tokyo-night",
        "#c0caf5", "#1a1b26",
        "#7aa2f7", "#565f89", "#c0caf5",
        "#565f89", "#c0caf5", "#7dcfff", "#ff9e64",
        "#1a1b26", "#7aa2f7",
        "#414868", "#f7768e", "#565f89", "#9ece6a",
    },
    {
        "monokai",
        "#f8f8f2", "#272822",
        "#66d9ef", "#75715e", "#f8f8f2",
        "#75715e", "#f8f8f2", "#66d9ef", "#fd971f",
        "#272822", "#f92672",
        "#49483e", "#e6db74", "#75715e", "#a6e22e",
    },
};
#define PRESET_COUNT (sizeof(PRESETS) / sizeof(PRESETS[0]))

static int apply_theme_preset_by_name(tcd_config *c, const char *name) {
    if (!name) return 0;
    for (size_t i = 0; i < PRESET_COUNT; i++) {
        if (strcmp(PRESETS[i].name, name) == 0) {
            const theme_preset *p = &PRESETS[i];
            set_str(&c->theme.foreground,  p->foreground);
            set_str(&c->theme.background,  p->background);
            set_str(&c->theme.header_fg,   p->header_fg);
            set_str(&c->theme.footer_fg,   p->footer_fg);
            set_str(&c->theme.path_fg,     p->path_fg);
            set_str(&c->theme.index_fg,    p->index_fg);
            set_str(&c->theme.file_fg,     p->file_fg);
            set_str(&c->theme.dir_fg,      p->dir_fg);
            set_str(&c->theme.match_fg,    p->match_fg);
            set_str(&c->theme.selected_fg, p->selected_fg);
            set_str(&c->theme.selected_bg, p->selected_bg);
            set_str(&c->theme.border_fg,   p->border_fg);
            set_str(&c->theme.filter_fg,   p->filter_fg);
            set_str(&c->theme.hint_fg,     p->hint_fg);
            set_str(&c->theme.key_fg,      p->key_fg);
            return 1;
        }
    }
    return 0;
}

static void apply_theme_object(tcd_config *c, json_value *theme) {
    if (!theme || theme->type != JSON_OBJECT) return;
    apply_str_from_json(&c->theme.foreground,  theme, "foreground");
    apply_str_from_json(&c->theme.background,  theme, "background");
    apply_str_from_json(&c->theme.header_fg,   theme, "header_fg");
    apply_str_from_json(&c->theme.footer_fg,   theme, "footer_fg");
    apply_str_from_json(&c->theme.path_fg,     theme, "path_fg");
    apply_str_from_json(&c->theme.index_fg,    theme, "index_fg");
    apply_str_from_json(&c->theme.file_fg,     theme, "file_fg");
    apply_str_from_json(&c->theme.dir_fg,      theme, "dir_fg");
    apply_str_from_json(&c->theme.match_fg,    theme, "match_fg");
    apply_str_from_json(&c->theme.selected_fg, theme, "selected_fg");
    apply_str_from_json(&c->theme.selected_bg, theme, "selected_bg");
    apply_str_from_json(&c->theme.border_fg,   theme, "border_fg");
    apply_str_from_json(&c->theme.filter_fg,   theme, "filter_fg");
    apply_str_from_json(&c->theme.hint_fg,     theme, "hint_fg");
    apply_str_from_json(&c->theme.key_fg,      theme, "key_fg");
}

static json_value *find_user_theme(json_value *root, const char *name) {
    if (!name) return NULL;
    json_value *themes = json_get(root, "themes");
    if (!themes || themes->type != JSON_ARRAY) return NULL;
    for (size_t i = 0; i < themes->v.array.count; i++) {
        json_value *entry = themes->v.array.items[i];
        if (!entry || entry->type != JSON_OBJECT) continue;
        const char *preset = json_get_string(entry, "preset", NULL);
        if (preset && strcmp(preset, name) == 0) return entry;
    }
    return NULL;
}

static void resolve_theme(tcd_config *c, json_value *root) {
    json_value *theme_field = json_get(root, "theme");
    if (!theme_field || theme_field->type != JSON_STRING) return;
    const char *name = theme_field->v.string;
    apply_theme_preset_by_name(c, name);
    json_value *user = find_user_theme(root, name);
    if (user) apply_theme_object(c, user);
}

static void apply_keys(tcd_config *c, json_value *keys) {
    if (!keys || keys->type != JSON_OBJECT) return;
    apply_keybind_from_json(&c->k_up,             json_get(keys, "up"));
    apply_keybind_from_json(&c->k_down,           json_get(keys, "down"));
    apply_keybind_from_json(&c->k_enter,          json_get(keys, "enter"));
    apply_keybind_from_json(&c->k_back,           json_get(keys, "back"));
    apply_keybind_from_json(&c->k_commit,         json_get(keys, "commit"));
    apply_keybind_from_json(&c->k_commit_explore, json_get(keys, "commit_explore"));
    apply_keybind_from_json(&c->k_cancel,         json_get(keys, "cancel"));
    apply_keybind_from_json(&c->k_drives,         json_get(keys, "drives"));
    apply_keybind_from_json(&c->k_quit,           json_get(keys, "quit"));
    apply_keybind_from_json(&c->k_top,            json_get(keys, "top"));
    apply_keybind_from_json(&c->k_bottom,         json_get(keys, "bottom"));
    apply_keybind_from_json(&c->k_page_up,        json_get(keys, "page_up"));
    apply_keybind_from_json(&c->k_page_down,      json_get(keys, "page_down"));
    apply_keybind_from_json(&c->k_toggle_hidden,  json_get(keys, "toggle_hidden"));
    apply_keybind_from_json(&c->k_clear_filter,   json_get(keys, "clear_filter"));
    apply_keybind_from_json(&c->k_save_bookmark,  json_get(keys, "save_bookmark"));
}

static int bookmarks_has_name(const tcd_config *c, const char *name) {
    for (size_t i = 0; i < c->bookmarks.count; i++) {
        if (strcmp(c->bookmarks.names[i], name) == 0) return 1;
    }
    return 0;
}

static void bookmarks_append(tcd_config *c, const char *name, const char *path) {
    size_t new_count = c->bookmarks.count + 1;
    char **names = (char **)realloc(c->bookmarks.names, new_count * sizeof(char *));
    if (!names) return;
    c->bookmarks.names = names;

    char **paths = (char **)realloc(c->bookmarks.paths, new_count * sizeof(char *));
    if (!paths) return;
    c->bookmarks.paths = paths;

    c->bookmarks.names[c->bookmarks.count] = xstrdup(name);
    c->bookmarks.paths[c->bookmarks.count] = xstrdup(path);
    c->bookmarks.count = new_count;
}

static void apply_bookmarks(tcd_config *c, json_value *root) {
    json_value *bookmarks = json_get(root, "bookmarks");
    if (!bookmarks || bookmarks->type != JSON_OBJECT) return;

    size_t count = bookmarks->v.object.count;
    if (count == 0) return;

    c->bookmarks.names = (char **)calloc(count, sizeof(char *));
    c->bookmarks.paths = (char **)calloc(count, sizeof(char *));
    if (!c->bookmarks.names || !c->bookmarks.paths) return;

    for (size_t i = 0; i < count; i++) {
        json_value *value = bookmarks->v.object.values[i];
        if (!value || value->type != JSON_STRING) continue;
        c->bookmarks.names[c->bookmarks.count] = xstrdup(bookmarks->v.object.keys[i]);
        c->bookmarks.paths[c->bookmarks.count] = xstrdup(value->v.string);
        c->bookmarks.count++;
    }
}

static void apply_default_bookmarks(tcd_config *c) {
    if (!bookmarks_has_name(c, "tcd")) {
        char *exe_dir = fs_exe_dir();
        if (exe_dir) {
            bookmarks_append(c, "tcd", exe_dir);
            free(exe_dir);
        }
    }
}

static void apply_root(tcd_config *c, json_value *root) {
    if (!root || root->type != JSON_OBJECT) return;
    json_value *v;
    v = json_get(root, "show_index");      if (v) c->show_index      = json_bool(v);
    v = json_get(root, "show_hidden");     if (v) c->show_hidden     = json_bool(v);
    v = json_get(root, "show_size");       if (v) c->show_size       = json_bool(v);
    v = json_get(root, "wrap_navigation"); if (v) c->wrap_navigation = json_bool(v);
    v = json_get(root, "per_page");        if (v && v->type == JSON_NUMBER) {
        int n = (int)v->v.number;
        if (n >= 3 && n <= 200) c->per_page = n;
    }
    apply_str_from_json(&c->sort, root, "sort");
    resolve_theme(c, root);
    apply_keys(c, json_get(root, "keys"));
    apply_bookmarks(c, root);
}

size_t config_built_in_theme_count(void) {
    return PRESET_COUNT;
}

const char *config_built_in_theme_name(size_t index) {
    if (index >= PRESET_COUNT) return NULL;
    return PRESETS[index].name;
}

char *config_resolve_path(const char *override_path) {
    if (override_path && *override_path) {
        char *r = (char *)malloc(strlen(override_path) + 1);
        if (r) memcpy(r, override_path, strlen(override_path) + 1);
        return r;
    }
    char *exedir = fs_exe_dir();
    if (exedir) {
        char *portable = fs_join(exedir, "config.json");
        free(exedir);
        if (portable && fs_exists(portable)) return portable;
        free(portable);
    }
    return config_default_path();
}

char *config_default_path(void) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, buf) != S_OK) {
        const char *a = getenv("APPDATA");
        if (!a) return NULL;
        size_t n = strlen(a) + 32;
        char *r = (char *)malloc(n);
        if (!r) return NULL;
        snprintf(r, n, "%s\\tcd\\config.json", a);
        return r;
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *base = (char *)malloc((size_t)len);
    if (!base) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, base, len, NULL, NULL);
    size_t n = strlen(base) + 32;
    char *r = (char *)malloc(n);
    if (!r) { free(base); return NULL; }
    snprintf(r, n, "%s\\tcd\\config.json", base);
    free(base);
    return r;
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        size_t n = strlen(xdg) + 32;
        char *r = (char *)malloc(n);
        if (!r) return NULL;
        snprintf(r, n, "%s/tcd/config.json", xdg);
        return r;
    }
    char *home = fs_home();
    if (!home) return NULL;
    size_t n = strlen(home) + 64;
    char *r = (char *)malloc(n);
    if (!r) { free(home); return NULL; }
    snprintf(r, n, "%s/.config/tcd/config.json", home);
    free(home);
    return r;
#endif
}

static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    if (l < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)l + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)l, f);
    fclose(f);
    buf[r] = 0;
    return buf;
}

tcd_config *config_load(const char *override_path, char *err, size_t err_sz) {
    tcd_config *c = (tcd_config *)calloc(1, sizeof(tcd_config));
    if (!c) {
        if (err && err_sz) snprintf(err, err_sz, "out of memory");
        return NULL;
    }
    apply_defaults(c);

    char *path_owned = config_resolve_path(override_path);
    const char *path = path_owned;

    if (path) {
        char *src = read_all(path);
        if (src) {
            char perr[128] = {0};
            json_value *root = json_parse(src, perr, sizeof(perr));
            free(src);
            if (root) {
                apply_root(c, root);
                json_free(root);
            } else if (err && err_sz) {
                snprintf(err, err_sz, "config %s: %s", path, perr);
            }
        }
    }

    apply_default_bookmarks(c);

    free(path_owned);
    return c;
}

void config_free(tcd_config *c) {
    if (!c) return;
    free(c->sort);
    free(c->path_separator_display);
    free(c->theme.foreground);
    free(c->theme.background);
    free(c->theme.header_fg);
    free(c->theme.footer_fg);
    free(c->theme.path_fg);
    free(c->theme.index_fg);
    free(c->theme.file_fg);
    free(c->theme.dir_fg);
    free(c->theme.match_fg);
    free(c->theme.selected_fg);
    free(c->theme.selected_bg);
    free(c->theme.border_fg);
    free(c->theme.filter_fg);
    free(c->theme.hint_fg);
    free(c->theme.key_fg);
    keybind_clear(&c->k_up);
    keybind_clear(&c->k_down);
    keybind_clear(&c->k_enter);
    keybind_clear(&c->k_back);
    keybind_clear(&c->k_commit);
    keybind_clear(&c->k_commit_explore);
    keybind_clear(&c->k_cancel);
    keybind_clear(&c->k_drives);
    keybind_clear(&c->k_quit);
    keybind_clear(&c->k_top);
    keybind_clear(&c->k_bottom);
    keybind_clear(&c->k_page_up);
    keybind_clear(&c->k_page_down);
    keybind_clear(&c->k_toggle_hidden);
    keybind_clear(&c->k_clear_filter);
    keybind_clear(&c->k_save_bookmark);

    for (size_t i = 0; i < c->bookmarks.count; i++) {
        free(c->bookmarks.names[i]);
        free(c->bookmarks.paths[i]);
    }
    free(c->bookmarks.names);
    free(c->bookmarks.paths);

    free(c);
}

const char *config_bookmark_get(const tcd_config *c, const char *name) {
    if (!c || !name) return NULL;
    for (size_t i = 0; i < c->bookmarks.count; i++) {
        if (strcmp(c->bookmarks.names[i], name) == 0) {
            return c->bookmarks.paths[i];
        }
    }
    return NULL;
}

int config_bookmark_save(const char *override_path, const char *name, const char *path) {
    if (!name || !*name || !path || !*path) return -1;

    char *resolved = config_resolve_path(override_path);
    if (!resolved) return -1;

    json_value *root = NULL;
    char *src = read_all(resolved);
    if (src) {
        char err[128];
        root = json_parse(src, err, sizeof(err));
        free(src);
    }
    if (!root) {
        root = json_make_object();
        if (!root) { free(resolved); return -1; }
    }
    if (root->type != JSON_OBJECT) {
        json_free(root);
        root = json_make_object();
        if (!root) { free(resolved); return -1; }
    }

    json_value *bookmarks = json_get(root, "bookmarks");
    if (!bookmarks || bookmarks->type != JSON_OBJECT) {
        bookmarks = json_make_object();
        if (!bookmarks) { json_free(root); free(resolved); return -1; }
        if (json_object_set(root, "bookmarks", bookmarks) != 0) {
            json_free(bookmarks);
            json_free(root);
            free(resolved);
            return -1;
        }
    }

    json_value *path_value = json_make_string(path);
    if (!path_value) { json_free(root); free(resolved); return -1; }
    if (json_object_set(bookmarks, name, path_value) != 0) {
        json_free(path_value);
        json_free(root);
        free(resolved);
        return -1;
    }

    int rc = json_write_to_file(root, resolved);
    json_free(root);
    free(resolved);
    return rc;
}

static int icmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int parse_modifier_chain(const char *spec, int *ctrl, int *alt, int *shift, const char **base) {
    *ctrl = *alt = *shift = 0;
    const char *p = spec;
    while (1) {
        const char *plus = strchr(p, '+');
        if (!plus) break;
        size_t mlen = (size_t)(plus - p);
        if      (mlen == 4 && strncasecmp(p, "ctrl", 4) == 0) *ctrl = 1;
        else if (mlen == 1 && (p[0] == 'c' || p[0] == 'C'))   *ctrl = 1;
        else if (mlen == 3 && strncasecmp(p, "alt", 3) == 0)  *alt = 1;
        else if (mlen == 4 && strncasecmp(p, "meta", 4) == 0) *alt = 1;
        else if (mlen == 5 && strncasecmp(p, "shift", 5) == 0) *shift = 1;
        else break;
        p = plus + 1;
    }
    *base = p;
    return 0;
}

int config_key_matches(const tcd_keybind *k, const char *name, int ctrl, int alt, int shift) {
    if (!k || !name) return 0;
    for (size_t i = 0; i < k->count; i++) {
        const char *spec = k->items[i];
        if (!spec) continue;
        int sc, sa, ss;
        const char *base;
        parse_modifier_chain(spec, &sc, &sa, &ss, &base);
        if (sc != ctrl) continue;
        if (sa != alt) continue;
        if (ss != shift) continue;
        if (icmp(base, name) == 0) return 1;
    }
    return 0;
}
