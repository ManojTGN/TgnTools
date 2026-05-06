#include "config.h"
#include "fs.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TCD_VERSION
#  define TCD_VERSION "dev"
#endif
#ifndef TCD_COMMIT
#  define TCD_COMMIT "unknown"
#endif

static void print_usage(void) {
    fprintf(stderr,
        "tcd - TGN CD: terminal directory navigator\n"
        "\n"
        "Usage: tcd [path] [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  [path]                 directory to start in (defaults to cwd)\n"
        "  --config FILE          use FILE instead of the auto-discovered config\n"
        "  --print-config-path    print the resolved config path and exit\n"
        "  --list-themes          list built-in theme names and exit\n"
        "  --version, -V          print version and exit\n"
        "  --help, -h             show this help and exit\n");
}

int main(int argc, char **argv) {
    const char *initial = NULL;
    const char *config_override = NULL;

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
        if (strcmp(a, "--print-config-path") == 0) {
            char *p = config_resolve_path(config_override);
            if (p) { printf("%s\n", p); free(p); }
            return 0;
        }
        if (strcmp(a, "--list-themes") == 0) {
            size_t n = config_built_in_theme_count();
            for (size_t i = 0; i < n; i++) {
                const char *nm = config_built_in_theme_name(i);
                if (nm) printf("%s\n", nm);
            }
            return 0;
        }
        if (strcmp(a, "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "tcd: --config requires a path argument\n");
                return 2;
            }
            config_override = argv[++i];
            continue;
        }
        if (a[0] == '-') {
            fprintf(stderr, "tcd: unknown option '%s'\n", a);
            return 2;
        }
        initial = a;
    }

    char err[256] = {0};
    tcd_config *cfg = config_load(config_override, err, sizeof(err));
    if (!cfg) {
        fprintf(stderr, "tcd: %s\n", err[0] ? err : "config load failed");
        return 1;
    }
    if (err[0]) {
        fprintf(stderr, "tcd: warning: %s (continuing with defaults for invalid keys)\n", err);
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
