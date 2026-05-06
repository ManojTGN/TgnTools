#include "ui.h"
#include "term.h"
#include "input.h"
#include "fs.h"
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILTER 128

typedef enum {
    MODE_BROWSE,
    MODE_LOCATIONS
} view_mode;

typedef enum {
    ACTION_CONTINUE       = 0,
    ACTION_COMMIT         = 1,
    ACTION_CANCEL         = 2,
    ACTION_COMMIT_EXPLORE = 3,
    ACTION_INTERRUPT      = -1
} input_action;

typedef struct {
    const tcd_config *cfg;

    char *cwd;
    fs_entry *entries;
    size_t entry_count;

    int *visible;
    size_t visible_count;
    size_t visible_cap;

    int selected;
    int top;

    char filter[MAX_FILTER];
    int filter_len;

    view_mode mode;
    char **locations_paths;
    char **locations_labels;
    size_t locations_count;
    int loc_selected;
    int loc_top;
    char loc_filter[MAX_FILTER];
    int loc_filter_len;

    int term_cols, term_rows;

    int show_hidden_runtime;
} ui_state;

static int icontains(const char *hay, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!hay) return 0;
    size_t hl = strlen(hay);
    size_t nl = strlen(needle);
    if (nl > hl) return 0;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t j = 0;
        for (; j < nl; j++) {
            int hc = tolower((unsigned char)hay[i + j]);
            int nc = tolower((unsigned char)needle[j]);
            if (hc != nc) break;
        }
        if (j == nl) return 1;
    }
    return 0;
}

static void rebuild_visible(ui_state *st) {
    if (st->visible_cap < st->entry_count + 1) {
        free(st->visible);
        size_t cap = st->entry_count + 16;
        st->visible = (int *)calloc(cap, sizeof(int));
        st->visible_cap = cap;
    }
    st->visible_count = 0;
    for (size_t i = 0; i < st->entry_count; i++) {
        if (icontains(st->entries[i].name, st->filter)) {
            st->visible[st->visible_count++] = (int)i;
        }
    }
    if (st->selected >= (int)st->visible_count) st->selected = (int)st->visible_count - 1;
    if (st->selected < 0) st->selected = 0;
    int per_page = st->cfg->per_page;
    if (per_page < 3) per_page = 3;
    if (st->selected < st->top) st->top = st->selected;
    if (st->selected >= st->top + per_page) st->top = st->selected - per_page + 1;
    if (st->top < 0) st->top = 0;
}

static int load_dir(ui_state *st, const char *path) {
    fs_entry *arr = NULL;
    size_t n = 0;
    int show_hidden = st->cfg->show_hidden || st->show_hidden_runtime;
    if (fs_list(path, show_hidden, &arr, &n) != 0) return -1;
    fs_sort_entries(arr, n, st->cfg->sort);

    fs_free_list(st->entries, st->entry_count);
    st->entries = arr;
    st->entry_count = n;
    free(st->cwd);
    st->cwd = NULL;
    st->cwd = (char *)malloc(strlen(path) + 1);
    if (st->cwd) memcpy(st->cwd, path, strlen(path) + 1);
    st->selected = 0;
    st->top = 0;
    st->filter[0] = 0;
    st->filter_len = 0;
    rebuild_visible(st);
    return 0;
}

static void rebuild_locations_visible(ui_state *st) {
    (void)st;
}

static int load_locations(ui_state *st) {
    fs_free_locations(st->locations_paths, st->locations_labels, st->locations_count);
    st->locations_paths = NULL;
    st->locations_labels = NULL;
    st->locations_count = 0;
    if (fs_list_locations(&st->locations_paths, &st->locations_labels, &st->locations_count) != 0) return -1;
    st->loc_selected = 0;
    st->loc_top = 0;
    st->loc_filter[0] = 0;
    st->loc_filter_len = 0;
    rebuild_locations_visible(st);
    return 0;
}

static const tcd_theme *theme(const ui_state *st) { return &st->cfg->theme; }

static void draw_path_bar(ui_state *st) {
    term_move(1, 1);
    term_set_bg(theme(st)->background);
    term_clear_line();
    term_set_fg(theme(st)->header_fg);
    term_set_bold(1);
    term_write("  TCD ");
    term_set_bold(0);
    term_set_fg(theme(st)->path_fg);
    if (st->mode == MODE_LOCATIONS) {
        term_write("[locations]");
    } else {
        term_write(st->cwd ? st->cwd : "");
    }
    term_reset_attrs();
}

static int max_visible_rows(ui_state *st);

static void draw_filter_line(ui_state *st) {
    int row = 2;
    term_move(row, 1);
    term_set_bg(theme(st)->background);
    term_clear_line();
    term_write("  ");

    if (st->mode == MODE_BROWSE) {
        if (st->filter_len > 0) {
            term_set_fg(theme(st)->filter_fg);
            term_writef("/ %s", st->filter);
        } else {
            term_set_fg(theme(st)->hint_fg);
            term_write("/ type for filter");
        }
    } else if (st->mode == MODE_LOCATIONS) {
        term_set_fg(theme(st)->hint_fg);
        term_write("(locations)");
    }

    int total, top;
    if (st->mode == MODE_BROWSE) {
        total = (int)st->visible_count;
        top = st->top;
    } else {
        total = (int)st->locations_count;
        top = st->loc_top;
    }
    int per = max_visible_rows(st);
    int from = total > 0 ? top + 1 : 0;
    int to = top + per;
    if (to > total) to = total;

    char buf[64];
    int n = snprintf(buf, sizeof(buf), "[%d - %d]:%d", from, to, total);
    int col = st->term_cols - n - 1;
    if (col < 4) col = 4;
    term_move(row, col);
    term_set_fg(theme(st)->hint_fg);
    term_write_n(buf, (size_t)n);

    term_reset_attrs();
}

static void draw_separator(ui_state *st, int row) {
    term_move(row, 1);
    term_set_bg(theme(st)->background);
    term_clear_line();
    term_set_fg(theme(st)->border_fg);
    int cols = st->term_cols;
    if (cols > 200) cols = 200;
    for (int i = 0; i < cols; i++) term_write("-");
    term_reset_attrs();
}

static void draw_entry_row(ui_state *st, int row, int idx_in_visible, int selected) {
    int real = st->visible[idx_in_visible];
    fs_entry *e = &st->entries[real];

    term_move(row, 1);
    if (selected) {
        term_set_fg(theme(st)->selected_fg);
        term_set_bg(theme(st)->selected_bg);
    } else {
        term_set_bg(theme(st)->background);
    }
    term_clear_line();

    term_write("  ");

    if (st->cfg->show_index) {
        if (!selected) term_set_fg(theme(st)->index_fg);
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%4d  ", idx_in_visible + 1);
        term_write_n(buf, (size_t)n);
    }

    if (!selected) {
        term_set_fg(e->is_dir ? theme(st)->dir_fg : theme(st)->file_fg);
    }
    term_write(e->is_dir ? "\xf0\x9f\x93\x81 " : "\xf0\x9f\x93\x84 ");

    int icon_cells = 3;
    int idx_cells  = st->cfg->show_index ? 6 : 0;
    int size_cells = st->cfg->show_size ? 14 : 0;
    int avail = st->term_cols - 2 - idx_cells - icon_cells - size_cells - 2;
    if (avail < 8) avail = 8;
    const char *name = e->name;
    int name_len = (int)strlen(name);
    if (name_len <= avail) {
        term_write(name);
        for (int i = name_len; i < avail; i++) term_write(" ");
    } else {
        term_write_n(name, (size_t)(avail - 1));
        term_write("~");
    }

    if (st->cfg->show_size && !e->is_dir) {
        char sb[32];
        long long sz = e->size;
        const char *unit = "B";
        double v = (double)sz;
        if (v >= 1024.0)            { v /= 1024.0; unit = "K"; }
        if (v >= 1024.0)            { v /= 1024.0; unit = "M"; }
        if (v >= 1024.0)            { v /= 1024.0; unit = "G"; }
        if (v >= 1024.0)            { v /= 1024.0; unit = "T"; }
        int n = snprintf(sb, sizeof(sb), "  %8.1f %s", v, unit);
        term_write_n(sb, (size_t)n);
    }

    term_reset_attrs();
}

static void draw_location_row(ui_state *st, int row, int idx, int selected) {
    term_move(row, 1);
    if (selected) {
        term_set_fg(theme(st)->selected_fg);
        term_set_bg(theme(st)->selected_bg);
    } else {
        term_set_bg(theme(st)->background);
    }
    term_clear_line();

    term_write("  ");
    if (st->cfg->show_index) {
        if (!selected) term_set_fg(theme(st)->index_fg);
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%4d  ", idx + 1);
        term_write_n(buf, (size_t)n);
    }
    if (!selected) term_set_fg(theme(st)->dir_fg);
    term_write("\xf0\x9f\x93\x81 ");
    const char *label = st->locations_labels[idx];
    if (!label) label = st->locations_paths[idx];
    term_write(label ? label : "");
    term_reset_attrs();
}

static void draw_footer_kv(ui_state *st, const char *key, const char *label) {
    term_set_fg(theme(st)->key_fg);
    term_set_bold(1);
    term_write(key);
    term_set_bold(0);
    term_set_fg(theme(st)->footer_fg);
    term_writef(" %s   ", label);
}

static void draw_footer(ui_state *st) {
    int row = st->term_rows;
    term_move(row, 1);
    term_set_bg(theme(st)->background);
    term_clear_line();
    term_write("  ");
    if (st->mode == MODE_LOCATIONS) {
        draw_footer_kv(st, "enter", "open");
        draw_footer_kv(st, "esc",   "back");
    } else {
        draw_footer_kv(st, "enter",            "open");
        draw_footer_kv(st, "ctrl+enter",       "cd");
        draw_footer_kv(st, "ctrl+shift+enter", "cd+explore");
        draw_footer_kv(st, "esc",              "cancel");
        draw_footer_kv(st, "tab",              "locations");
        draw_footer_kv(st, "bksp",             "parent");
    }
    term_reset_attrs();
}

static int max_visible_rows(ui_state *st) {
    int rows = st->term_rows - 5;
    if (rows < 3) rows = 3;
    int cap = st->cfg->per_page;
    if (cap < 3) cap = 3;
    return rows < cap ? rows : cap;
}

static void clamp_view_browse(ui_state *st) {
    int per = max_visible_rows(st);
    if (st->visible_count == 0) { st->selected = 0; st->top = 0; return; }
    if (st->selected >= (int)st->visible_count) st->selected = (int)st->visible_count - 1;
    if (st->selected < 0) st->selected = 0;
    if (st->selected < st->top) st->top = st->selected;
    if (st->selected >= st->top + per) st->top = st->selected - per + 1;
    if (st->top < 0) st->top = 0;
    if ((size_t)(st->top + per) > st->visible_count) {
        if (st->visible_count > (size_t)per) st->top = (int)st->visible_count - per;
        else st->top = 0;
    }
}

static void clamp_view_locations(ui_state *st) {
    int per = max_visible_rows(st);
    if (st->locations_count == 0) { st->loc_selected = 0; st->loc_top = 0; return; }
    if (st->loc_selected >= (int)st->locations_count) st->loc_selected = (int)st->locations_count - 1;
    if (st->loc_selected < 0) st->loc_selected = 0;
    if (st->loc_selected < st->loc_top) st->loc_top = st->loc_selected;
    if (st->loc_selected >= st->loc_top + per) st->loc_top = st->loc_selected - per + 1;
    if (st->loc_top < 0) st->loc_top = 0;
}

static void render(ui_state *st) {
    term_get_size(&st->term_cols, &st->term_rows);
    if (st->mode == MODE_BROWSE) clamp_view_browse(st);
    else                          clamp_view_locations(st);

    term_set_fg(theme(st)->foreground);
    term_set_bg(theme(st)->background);
    term_clear();

    draw_path_bar(st);
    draw_filter_line(st);
    draw_separator(st, 3);

    int per = max_visible_rows(st);
    int start_row = 4;

    if (st->mode == MODE_BROWSE) {
        if (st->visible_count == 0) {
            term_move(start_row, 1);
            term_set_bg(theme(st)->background);
            term_clear_line();
            if (st->filter_len > 0) {
                term_set_fg(theme(st)->hint_fg);
                term_write("  No match for ");
                term_set_fg(theme(st)->match_fg);
                term_writef("\"%s\"", st->filter);
                term_set_fg(theme(st)->hint_fg);
                term_write("  \xc2\xb7  ");
                term_set_fg(theme(st)->key_fg);
                term_write("backspace");
                term_set_fg(theme(st)->hint_fg);
                term_write(" to clear  \xc2\xb7  ");
                term_set_fg(theme(st)->key_fg);
                term_write("ctrl+u");
                term_set_fg(theme(st)->hint_fg);
                term_write(" to reset");
            } else {
                term_set_fg(theme(st)->hint_fg);
                term_write("  (this folder is empty)");
            }
            term_reset_attrs();
        } else {
            int end = st->top + per;
            if ((size_t)end > st->visible_count) end = (int)st->visible_count;
            for (int i = st->top; i < end; i++) {
                int row = start_row + (i - st->top);
                draw_entry_row(st, row, i, i == st->selected);
            }
        }
    } else {
        if (st->locations_count == 0) {
            term_move(start_row, 1);
            term_set_bg(theme(st)->background);
            term_clear_line();
            term_set_fg(theme(st)->hint_fg);
            term_write("  (no locations available)");
            term_reset_attrs();
        } else {
            int end = st->loc_top + per;
            if ((size_t)end > st->locations_count) end = (int)st->locations_count;
            for (int i = st->loc_top; i < end; i++) {
                int row = start_row + (i - st->loc_top);
                draw_location_row(st, row, i, i == st->loc_selected);
            }
        }
    }

    draw_footer(st);
    term_flush();
}

static const char *key_name_for_event(const key_event *k) {
    switch (k->type) {
        case KEY_UP: return "up";
        case KEY_DOWN: return "down";
        case KEY_LEFT: return "left";
        case KEY_RIGHT: return "right";
        case KEY_ENTER: return "enter";
        case KEY_ESC: return "esc";
        case KEY_TAB: return "tab";
        case KEY_SHIFT_TAB: return "shift+tab";
        case KEY_BACKSPACE: return "backspace";
        case KEY_DELETE: return "delete";
        case KEY_HOME: return "home";
        case KEY_END: return "end";
        case KEY_PAGE_UP: return "pageup";
        case KEY_PAGE_DOWN: return "pagedown";
        case KEY_INTERRUPT: return "ctrl+c";
        default: return NULL;
    }
}

static int key_matches_binding(const tcd_keybind *binding, const key_event *event) {
    if (event->type == KEY_CHAR) {
        char as_str[2] = { (char)tolower((int)event->ch), 0 };
        return config_key_matches(binding, as_str, event->ctrl, event->alt, 0);
    }

    const char *name = key_name_for_event(event);
    if (!name) return 0;
    return config_key_matches(binding, name, event->ctrl, event->alt, event->shift);
}

static void filter_append_char(char *buf, int *len, uint32_t ch) {
    if (ch < 0x20 || ch >= 0x80) return;
    if (*len >= MAX_FILTER - 1) return;
    buf[(*len)++] = (char)ch;
    buf[*len] = 0;
}

static void filter_backspace(char *buf, int *len) {
    if (*len <= 0) { buf[0] = 0; return; }
    (*len)--;
    buf[*len] = 0;
}

static input_action handle_browse(ui_state *st, key_event event) {
    const tcd_config *cfg = st->cfg;

    if (event.type == KEY_INTERRUPT) return ACTION_INTERRUPT;

    /* Unmodified printable chars always go to the filter. */
    if (event.type == KEY_CHAR && !event.ctrl && !event.alt
        && event.ch >= 0x20 && event.ch < 0x80)
    {
        filter_append_char(st->filter, &st->filter_len, event.ch);
        rebuild_visible(st);
        return ACTION_CONTINUE;
    }

    if (event.type == KEY_BACKSPACE) {
        if (st->filter_len > 0) {
            filter_backspace(st->filter, &st->filter_len);
            rebuild_visible(st);
        } else {
            char *parent = fs_parent(st->cwd ? st->cwd : ".");
            if (parent) {
                if (strcmp(parent, st->cwd ? st->cwd : "") != 0) load_dir(st, parent);
                free(parent);
            }
        }
        return ACTION_CONTINUE;
    }

    if (key_matches_binding(&cfg->k_cancel,         &event)) return ACTION_CANCEL;
    if (key_matches_binding(&cfg->k_commit_explore, &event)) return ACTION_COMMIT_EXPLORE;
    if (key_matches_binding(&cfg->k_commit,         &event)) return ACTION_COMMIT;

    if (key_matches_binding(&cfg->k_drives, &event)) {
        if (load_locations(st) == 0) st->mode = MODE_LOCATIONS;
        return ACTION_CONTINUE;
    }

    if (key_matches_binding(&cfg->k_up, &event)) {
        if (st->visible_count == 0) return ACTION_CONTINUE;
        if (st->selected > 0) st->selected--;
        else if (cfg->wrap_navigation) st->selected = (int)st->visible_count - 1;
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_down, &event)) {
        if (st->visible_count == 0) return ACTION_CONTINUE;
        if (st->selected < (int)st->visible_count - 1) st->selected++;
        else if (cfg->wrap_navigation) st->selected = 0;
        return ACTION_CONTINUE;
    }

    if (key_matches_binding(&cfg->k_top, &event)) {
        st->selected = 0;
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_bottom, &event)) {
        if (st->visible_count > 0) st->selected = (int)st->visible_count - 1;
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_page_up, &event)) {
        st->selected -= max_visible_rows(st);
        if (st->selected < 0) st->selected = 0;
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_page_down, &event)) {
        st->selected += max_visible_rows(st);
        if (st->visible_count > 0 && st->selected >= (int)st->visible_count) {
            st->selected = (int)st->visible_count - 1;
        }
        return ACTION_CONTINUE;
    }

    if (key_matches_binding(&cfg->k_toggle_hidden, &event)) {
        st->show_hidden_runtime = !st->show_hidden_runtime;
        load_dir(st, st->cwd);
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_clear_filter, &event)) {
        st->filter[0] = 0;
        st->filter_len = 0;
        rebuild_visible(st);
        return ACTION_CONTINUE;
    }

    if (key_matches_binding(&cfg->k_enter, &event)) {
        if (st->visible_count == 0) return ACTION_CONTINUE;
        int entry_index = st->visible[st->selected];
        fs_entry *entry = &st->entries[entry_index];
        if (!entry->is_dir) return ACTION_CONTINUE;
        char *next = fs_join(st->cwd, entry->name);
        if (next) {
            if (load_dir(st, next) != 0) load_dir(st, st->cwd);
            free(next);
        }
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_back, &event)) {
        char *parent = fs_parent(st->cwd ? st->cwd : ".");
        if (parent) {
            if (strcmp(parent, st->cwd ? st->cwd : "") != 0) load_dir(st, parent);
            free(parent);
        }
        return ACTION_CONTINUE;
    }

    return ACTION_CONTINUE;
}

static input_action handle_locations(ui_state *st, key_event event) {
    const tcd_config *cfg = st->cfg;

    if (event.type == KEY_INTERRUPT) return ACTION_INTERRUPT;

    if (event.type == KEY_ESC || key_matches_binding(&cfg->k_drives, &event)) {
        st->mode = MODE_BROWSE;
        return ACTION_CONTINUE;
    }

    if (key_matches_binding(&cfg->k_up, &event)) {
        if (st->loc_selected > 0) st->loc_selected--;
        else if (cfg->wrap_navigation) st->loc_selected = (int)st->locations_count - 1;
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_down, &event)) {
        if (st->loc_selected < (int)st->locations_count - 1) st->loc_selected++;
        else if (cfg->wrap_navigation) st->loc_selected = 0;
        return ACTION_CONTINUE;
    }

    if (key_matches_binding(&cfg->k_top, &event)) {
        st->loc_selected = 0;
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_bottom, &event)) {
        if (st->locations_count > 0) st->loc_selected = (int)st->locations_count - 1;
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_page_up, &event)) {
        st->loc_selected -= max_visible_rows(st);
        if (st->loc_selected < 0) st->loc_selected = 0;
        return ACTION_CONTINUE;
    }
    if (key_matches_binding(&cfg->k_page_down, &event)) {
        st->loc_selected += max_visible_rows(st);
        if (st->locations_count > 0 && st->loc_selected >= (int)st->locations_count) {
            st->loc_selected = (int)st->locations_count - 1;
        }
        return ACTION_CONTINUE;
    }

    if (event.type == KEY_ENTER || key_matches_binding(&cfg->k_enter, &event)) {
        if (st->locations_count == 0) return ACTION_CONTINUE;
        const char *target_path = st->locations_paths[st->loc_selected];
        if (target_path && load_dir(st, target_path) == 0) {
            st->mode = MODE_BROWSE;
        }
        return ACTION_CONTINUE;
    }

    return ACTION_CONTINUE;
}

int ui_run(const tcd_config *cfg, const char *initial_path,
           char **out_chosen, int *out_open_explorer) {
    ui_state st;
    memset(&st, 0, sizeof(st));
    st.cfg = cfg;
    if (out_open_explorer) *out_open_explorer = 0;

    char *start = initial_path ? (char *)initial_path : NULL;
    char *owned_start = NULL;
    if (!start || !*start) {
        owned_start = fs_cwd();
        start = owned_start;
    }
    if (!start) return 1;

    if (load_dir(&st, start) != 0) {
        free(owned_start);
        fprintf(stderr, "tcd: cannot list %s\n", start);
        return 1;
    }
    free(owned_start);
    st.mode = MODE_BROWSE;

    term_init();

    int rc = 0;
    while (1) {
        render(&st);
        key_event k = input_read();
        if (k.type == KEY_NONE) continue;
        if (k.type == KEY_RESIZE) continue;
        input_action action = (st.mode == MODE_BROWSE)
            ? handle_browse(&st, k)
            : handle_locations(&st, k);

        if (action == ACTION_COMMIT || action == ACTION_COMMIT_EXPLORE) {
            size_t len = strlen(st.cwd);
            *out_chosen = (char *)malloc(len + 1);
            if (*out_chosen) memcpy(*out_chosen, st.cwd, len + 1);
            if (action == ACTION_COMMIT_EXPLORE && out_open_explorer) {
                *out_open_explorer = 1;
            }
            rc = 0;
            break;
        }
        if (action == ACTION_CANCEL) {
            *out_chosen = NULL;
            rc = 0;
            break;
        }
        if (action == ACTION_INTERRUPT) {
            rc = 130;
            break;
        }
    }

    term_shutdown();

    fs_free_list(st.entries, st.entry_count);
    free(st.cwd);
    free(st.visible);
    fs_free_locations(st.locations_paths, st.locations_labels, st.locations_count);
    return rc;
}
