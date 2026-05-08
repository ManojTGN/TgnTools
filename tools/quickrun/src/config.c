#include "config.h"
#include "json.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <unistd.h>
#  include <pwd.h>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#    include <stdint.h>
#  endif
#endif

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

static char *exe_path(void) {
#ifdef _WIN32
    wchar_t wbuf[1024];
    DWORD n = GetModuleFileNameW(NULL, wbuf, (DWORD)(sizeof(wbuf)/sizeof(wbuf[0])));
    if (n == 0 || n >= sizeof(wbuf)/sizeof(wbuf[0])) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *out = (char *)malloc((size_t)len);
    if (!out) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, out, len, NULL, NULL);
    return out;
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return NULL;
    char *real = realpath(buf, NULL);
    return real ? real : xstrdup(buf);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return NULL;
    buf[n] = 0;
    return xstrdup(buf);
#endif
}

static char *path_parent(const char *path) {
    if (!path) return NULL;
    size_t l = strlen(path);
    char *r = (char *)malloc(l + 1);
    if (!r) return NULL;
    memcpy(r, path, l + 1);
    while (l > 0 && r[l - 1] != '/' && r[l - 1] != '\\') r[--l] = 0;
    if (l > 1) r[l - 1] = 0;
    return r;
}

static char *exe_dir(void) {
    char *p = exe_path();
    if (!p) return NULL;
    char *d = path_parent(p);
    free(p);
    return d;
}

static char *path_join(const char *base, const char *child) {
    if (!base) return xstrdup(child);
    if (!child) return xstrdup(base);
    size_t bl = strlen(base);
    size_t cl = strlen(child);
    int needs_sep = (bl > 0 && base[bl - 1] != '/' && base[bl - 1] != '\\');
    char *r = (char *)malloc(bl + cl + 2);
    if (!r) return NULL;
    memcpy(r, base, bl);
    size_t off = bl;
#ifdef _WIN32
    if (needs_sep) r[off++] = '\\';
#else
    if (needs_sep) r[off++] = '/';
#endif
    memcpy(r + off, child, cl);
    r[off + cl] = 0;
    return r;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static char *user_config_path(void) {
#ifdef _WIN32
    wchar_t wbuf[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, wbuf) != S_OK) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *base = (char *)malloc((size_t)len);
    if (!base) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, base, len, NULL, NULL);
    size_t n = strlen(base) + 32;
    char *r = (char *)malloc(n);
    if (!r) { free(base); return NULL; }
    snprintf(r, n, "%s\\quickrun\\config.json", base);
    free(base);
    return r;
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        size_t n = strlen(xdg) + 32;
        char *r = (char *)malloc(n);
        if (!r) return NULL;
        snprintf(r, n, "%s/quickrun/config.json", xdg);
        return r;
    }
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *p = getpwuid(getuid());
        if (p) home = p->pw_dir;
    }
    if (!home) return NULL;
    size_t n = strlen(home) + 32;
    char *r = (char *)malloc(n);
    if (!r) return NULL;
    snprintf(r, n, "%s/.config/quickrun/config.json", home);
    return r;
#endif
}

char *quickrun_default_log_path(void) {
    char *dir = exe_dir();
    if (!dir) return NULL;
    char *log = path_join(dir, "quickrun.log");
    free(dir);
    return log;
}

char *quickrun_config_resolve_path(const char *override_path) {
    if (override_path && *override_path) return xstrdup(override_path);

    char *dir = exe_dir();
    if (dir) {
        char *portable = path_join(dir, "config.json");
        free(dir);
        if (portable && file_exists(portable)) return portable;
        free(portable);
    }
    return user_config_path();
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
    size_t n = fread(buf, 1, (size_t)l, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static int load_action(json_value *entry, action *out) {
    const char *kind_str = json_get_string(entry, "action", NULL);
    if (!kind_str) return -1;

    if (strcmp(kind_str, "run") == 0) {
        out->kind = ACTION_RUN;
        const char *cmd = json_get_string(entry, "command", NULL);
        if (!cmd) return -1;
        out->command = xstrdup(cmd);

        json_value *args = json_get(entry, "args");
        if (args && args->type == JSON_ARRAY) {
            size_t n = args->v.array.count;
            out->args = (char **)calloc(n + 1, sizeof(char *));
            if (!out->args) return -1;
            for (size_t i = 0; i < n; i++) {
                json_value *item = args->v.array.items[i];
                if (item && item->type == JSON_STRING) {
                    out->args[out->arg_count++] = xstrdup(item->v.string);
                }
            }
        }
        return 0;
    }

    if (strcmp(kind_str, "open") == 0) {
        out->kind = ACTION_OPEN;
        const char *target = json_get_string(entry, "target", NULL);
        if (!target) return -1;
        out->target = xstrdup(target);
        return 0;
    }

    return -1;
}

static int load_bindings(quickrun_config *c, json_value *root) {
    json_value *list = json_get(root, "bindings");
    if (!list || list->type != JSON_ARRAY) return 0;

    size_t n = list->v.array.count;
    if (n == 0) return 0;

    c->bindings = (binding *)calloc(n, sizeof(binding));
    if (!c->bindings) return -1;

    for (size_t i = 0; i < n; i++) {
        json_value *entry = list->v.array.items[i];
        if (!entry || entry->type != JSON_OBJECT) continue;

        const char *spec = json_get_string(entry, "keys", NULL);
        if (!spec) {
            ql_log("config: binding %zu missing 'keys' field, skipping", i);
            continue;
        }

        binding *b = &c->bindings[c->binding_count];
        if (keyspec_parse(spec, &b->key) != 0) {
            ql_log("config: invalid keyspec '%s', skipping", spec);
            continue;
        }
        if (load_action(entry, &b->act) != 0) {
            ql_log("config: invalid action for '%s', skipping", spec);
            action_free(&b->act);
            continue;
        }
        c->binding_count++;
    }
    return 0;
}

quickrun_config *quickrun_config_load(const char *override_path, char *err, size_t err_sz) {
    quickrun_config *c = (quickrun_config *)calloc(1, sizeof(quickrun_config));
    if (!c) {
        if (err && err_sz) snprintf(err, err_sz, "out of memory");
        return NULL;
    }

    char *path = quickrun_config_resolve_path(override_path);
    if (!path) return c;

    char *src = read_all(path);
    if (!src) { free(path); return c; }

    char perr[128] = {0};
    json_value *root = json_parse(src, perr, sizeof(perr));
    free(src);
    if (!root) {
        if (err && err_sz) snprintf(err, err_sz, "config %s: %s", path, perr);
        free(path);
        return c;
    }

    const char *log_file = json_get_string(root, "log_file", NULL);
    if (log_file) c->log_file = xstrdup(log_file);

    load_bindings(c, root);

    json_free(root);
    free(path);
    return c;
}

void quickrun_config_free(quickrun_config *c) {
    if (!c) return;
    free(c->log_file);
    for (size_t i = 0; i < c->binding_count; i++) {
        action_free(&c->bindings[i].act);
    }
    free(c->bindings);
    free(c);
}
