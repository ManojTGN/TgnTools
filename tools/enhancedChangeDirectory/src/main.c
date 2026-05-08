#include "config.h"
#include "fs.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#endif

#ifndef TCD_VERSION
#  define TCD_VERSION "dev"
#endif
#ifndef TCD_COMMIT
#  define TCD_COMMIT "unknown"
#endif

static void enable_ansi_on_stdout(void) {
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode)) {
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

static void repeat_string(const char *s, int count) {
    for (int i = 0; i < count; i++) fputs(s, stdout);
}

static int print_config_view(void) {
    enable_ansi_on_stdout();

    char *path = config_resolve_path(NULL);
    if (!path) {
        fprintf(stderr, "tcd: cannot resolve config path\n");
        return 1;
    }

    int exists = fs_exists(path);

    long file_size = -1;
    if (exists) {
        FILE *f = fopen(path, "rb");
        if (f) {
            if (fseek(f, 0, SEEK_END) == 0) file_size = ftell(f);
            fclose(f);
        }
    }

    const char *cyan       = "\x1b[36m";
    const char *bold_white = "\x1b[1;37m";
    const char *gray       = "\x1b[90m";
    const char *yellow     = "\x1b[33m";
    const char *reset      = "\x1b[0m";

    const char *horiz     = "\xe2\x94\x80"; /* ─ */
    const char *corner_tl = "\xe2\x94\x8c"; /* ┌ */
    const char *corner_tr = "\xe2\x94\x90"; /* ┐ */
    const char *corner_bl = "\xe2\x94\x94"; /* └ */
    const char *corner_br = "\xe2\x94\x98"; /* ┘ */
    const char *vert      = "\xe2\x94\x82"; /* │ */

    const char *status_text;
    char size_buf[64];
    if (exists) {
        if (file_size >= 0) {
            snprintf(size_buf, sizeof(size_buf), "%ld bytes", file_size);
            status_text = size_buf;
        } else {
            status_text = "(unable to read size)";
        }
    } else {
        status_text = "file does not exist - built-in defaults are used";
    }

    int path_cells   = (int)strlen(path);
    int status_cells = (int)strlen(status_text);
    int title_cells  = (int)strlen(" tcd config ");

    /*
     * Body row layout: │  <content><pad>  │  = content + pad + 6 cells.
     * Top/bottom rows render `width` cells. So pad = width - content - 6 must
     * be >= 0, i.e. inner (= width - 4) >= content + 2 for every content row.
     */
    int inner = path_cells   + 2;
    if (status_cells + 2 > inner) inner = status_cells + 2;
    if (title_cells  + 4 > inner) inner = title_cells  + 4;
    const int min_inner = 66;
    if (inner < min_inner) inner = min_inner;
    int width = inner + 4;

    /* Top border: ┌── tcd config ─...─┐ */
    fputs(cyan, stdout);
    fputs(corner_tl, stdout);
    repeat_string(horiz, 2);
    fputs(" tcd config ", stdout);
    repeat_string(horiz, width - 16);
    fputs(corner_tr, stdout);
    fputs(reset, stdout);
    putchar('\n');

    /* Path line */
    printf("%s%s%s  %s%s%s", cyan, vert, reset, bold_white, path, reset);
    repeat_string(" ", inner - path_cells - 2);
    printf("  %s%s%s\n", cyan, vert, reset);

    /* Status line */
    const char *status_color = exists ? gray : yellow;
    printf("%s%s%s  %s%s%s", cyan, vert, reset, status_color, status_text, reset);
    repeat_string(" ", inner - status_cells - 2);
    printf("  %s%s%s\n", cyan, vert, reset);

    /* Bottom border */
    fputs(cyan, stdout);
    fputs(corner_bl, stdout);
    repeat_string(horiz, width - 2);
    fputs(corner_br, stdout);
    fputs(reset, stdout);
    putchar('\n');

    /* File contents below the box */
    if (exists && file_size > 0) {
        FILE *f = fopen(path, "rb");
        if (f) {
            putchar('\n');
            char buf[4096];
            size_t n;
            int last_byte = 0;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
                fwrite(buf, 1, n, stdout);
                last_byte = (unsigned char)buf[n - 1];
            }
            fclose(f);
            if (last_byte != '\n') putchar('\n');
        }
    }

    free(path);
    return 0;
}

static void print_usage(void) {
    fprintf(stderr,
        "tcd - TGN CD: terminal directory navigator\n"
        "\n"
        "Usage: tcd [path] [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  [path]                 directory to start in (defaults to cwd)\n"
        "  --config               print the resolved config file (path + contents)\n"
        "  --version, -V          print version and exit\n"
        "  --help, -h             show this help and exit\n");
}

int main(int argc, char **argv) {
    const char *initial = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
            printf("tcd %s (%s)\n", TCD_VERSION, TCD_COMMIT);
            return 0;
        }
        if (strcmp(a, "--config") == 0) {
            return print_config_view();
        }
        if (a[0] == '-') {
            fprintf(stderr, "tcd: unknown option '%s'\n", a);
            return 2;
        }
        initial = a;
    }

    char err[256] = {0};
    tcd_config *cfg = config_load(NULL, err, sizeof(err));
    if (!cfg) {
        fprintf(stderr, "tcd: %s\n", err[0] ? err : "config load failed");
        return 1;
    }
    if (err[0]) {
        fprintf(stderr, "tcd: warning: %s (continuing with defaults for invalid keys)\n", err);
    }

    if (initial) {
        if (initial[0] == '@') {
            const char *bookmark_name = initial + 1;
            const char *bookmark_path = config_bookmark_get(cfg, bookmark_name);
            if (!bookmark_path) {
                fprintf(stderr, "tcd: no bookmark named '%s'\n", bookmark_name);
                config_free(cfg);
                return 1;
            }
            initial = bookmark_path;
        } else if (!fs_is_dir(initial)) {
            const char *bookmark_path = config_bookmark_get(cfg, initial);
            if (bookmark_path) initial = bookmark_path;
        }
    }

    if (initial && !fs_is_dir(initial)) {
        fprintf(stderr, "tcd: not a directory: %s\n", initial);
        config_free(cfg);
        return 1;
    }

    char *chosen = NULL;
    int open_explorer = 0;
    int rc = ui_run(cfg, initial, &chosen, &open_explorer);
    config_free(cfg);

    if (rc == 0 && chosen) {
        if (open_explorer) fs_open_in_file_manager(chosen);
        fputs(chosen, stdout);
        fputc('\n', stdout);
    }
    free(chosen);
    return rc;
}
