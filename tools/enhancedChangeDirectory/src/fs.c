#include "fs.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#  include <shlobj.h>
#  include <shellapi.h>
#else
#  include <dirent.h>
#  include <pwd.h>
#  include <unistd.h>
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

void fs_free_list(fs_entry *entries, size_t count) {
    if (!entries) return;
    for (size_t i = 0; i < count; i++) free(entries[i].name);
    free(entries);
}

#ifdef _WIN32
static char *wcs_to_utf8(const wchar_t *w) {
    if (!w) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *out = (char *)malloc((size_t)len);
    if (!out) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, len, NULL, NULL);
    return out;
}

static wchar_t *utf8_to_wcs(const char *s) {
    if (!s) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t *out = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)len);
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, len);
    return out;
}
#endif

int fs_list(const char *path, int show_hidden, fs_entry **out_entries, size_t *out_count) {
    *out_entries = NULL;
    *out_count = 0;
    size_t cap = 64, n = 0;
    fs_entry *arr = (fs_entry *)calloc(cap, sizeof(fs_entry));
    if (!arr) return -1;

#ifdef _WIN32
    wchar_t *wpath = utf8_to_wcs(path);
    if (!wpath) { free(arr); return -1; }
    size_t wl = wcslen(wpath);
    wchar_t *wpat = (wchar_t *)malloc(sizeof(wchar_t) * (wl + 4));
    if (!wpat) { free(wpath); free(arr); return -1; }
    wcscpy(wpat, wpath);
    if (wl > 0 && wpat[wl - 1] != L'\\' && wpat[wl - 1] != L'/') wcscat(wpat, L"\\");
    wcscat(wpat, L"*");

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat, &fd);
    free(wpat);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE) { free(arr); return -1; }

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        int hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) ? 1 : 0;
        if (!show_hidden && hidden) continue;
        char *name = wcs_to_utf8(fd.cFileName);
        if (!name) continue;
        if (n >= cap) {
            cap *= 2;
            fs_entry *na = (fs_entry *)realloc(arr, cap * sizeof(fs_entry));
            if (!na) { free(name); break; }
            arr = na;
        }
        arr[n].name = name;
        arr[n].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        arr[n].size = ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(path);
    if (!d) { free(arr); return -1; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (!show_hidden && de->d_name[0] == '.') continue;
        char fullpath[4096];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, de->d_name);
        struct stat st;
        int is_dir = 0;
        long long size = 0;
        if (stat(fullpath, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            size = (long long)st.st_size;
        } else {
            continue;
        }
        if (n >= cap) {
            cap *= 2;
            fs_entry *na = (fs_entry *)realloc(arr, cap * sizeof(fs_entry));
            if (!na) break;
            arr = na;
        }
        arr[n].name = xstrdup(de->d_name);
        arr[n].is_dir = is_dir;
        arr[n].size = size;
        n++;
    }
    closedir(d);
#endif

    *out_entries = arr;
    *out_count = n;
    return 0;
}

char *fs_cwd(void) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2];
    DWORD r = GetCurrentDirectoryW(sizeof(buf) / sizeof(buf[0]), buf);
    if (r == 0) return xstrdup(".");
    return wcs_to_utf8(buf);
#else
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return xstrdup(".");
    return xstrdup(buf);
#endif
}

char *fs_home(void) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, buf) == S_OK) return wcs_to_utf8(buf);
    const char *u = getenv("USERPROFILE");
    return u ? xstrdup(u) : xstrdup("C:\\");
#else
    const char *h = getenv("HOME");
    if (h && *h) return xstrdup(h);
    struct passwd *p = getpwuid(getuid());
    if (p && p->pw_dir) return xstrdup(p->pw_dir);
    return xstrdup("/");
#endif
}

char *fs_join(const char *base, const char *child) {
    if (!base) return xstrdup(child ? child : "");
    if (!child || !*child) return xstrdup(base);
    size_t bl = strlen(base);
    size_t cl = strlen(child);
    int needs_sep = (bl > 0 && base[bl - 1] != '/' && base[bl - 1] != '\\');
    char *r = (char *)malloc(bl + cl + 2);
    if (!r) return NULL;
    memcpy(r, base, bl);
    size_t off = bl;
    if (needs_sep) r[off++] = TCD_PATH_SEP;
    memcpy(r + off, child, cl);
    r[off + cl] = 0;
    return r;
}

char *fs_parent(const char *path) {
    if (!path || !*path) return xstrdup("");
    size_t l = strlen(path);
    char *r = (char *)malloc(l + 1);
    if (!r) return NULL;
    memcpy(r, path, l + 1);

    while (l > 0 && (r[l - 1] == '/' || r[l - 1] == '\\')) {
#ifdef _WIN32
        if (l == 3 && r[1] == ':' && r[2] == '\\') break;
#endif
        if (l == 1) break;
        r[--l] = 0;
    }
    while (l > 0 && r[l - 1] != '/' && r[l - 1] != '\\') {
        r[--l] = 0;
    }
    if (l > 1) {
        while (l > 1 && (r[l - 1] == '/' || r[l - 1] == '\\')) {
#ifdef _WIN32
            if (l == 3 && r[1] == ':' && r[2] == '\\') break;
#endif
            r[--l] = 0;
        }
    }
    if (l == 0) {
#ifdef _WIN32
        free(r);
        return xstrdup("C:\\");
#else
        r[0] = '/'; r[1] = 0;
#endif
    }
    return r;
}

char *fs_normalize(const char *path) {
    if (!path) return NULL;
    return xstrdup(path);
}

int fs_is_dir(const char *path) {
#ifdef _WIN32
    wchar_t *w = utf8_to_wcs(path);
    if (!w) return 0;
    DWORD a = GetFileAttributesW(w);
    free(w);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

int fs_exists(const char *path) {
#ifdef _WIN32
    wchar_t *w = utf8_to_wcs(path);
    if (!w) return 0;
    DWORD a = GetFileAttributesW(w);
    free(w);
    return (a != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

int fs_list_locations(char ***out_paths, char ***out_labels, size_t *out_count) {
    size_t cap = 32, n = 0;
    char **paths = (char **)calloc(cap, sizeof(char *));
    char **labels = (char **)calloc(cap, sizeof(char *));
    if (!paths || !labels) { free(paths); free(labels); return -1; }

#define PUSH(p, l) do {                                               \
        if (n >= cap) {                                                \
            cap *= 2;                                                  \
            paths = (char **)realloc(paths, cap * sizeof(char *));     \
            labels = (char **)realloc(labels, cap * sizeof(char *));   \
            if (!paths || !labels) goto fail;                          \
        }                                                              \
        paths[n] = (p); labels[n] = (l); n++;                          \
    } while (0)

#ifdef _WIN32
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (mask & (1u << i)) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%c:\\", 'A' + i);
            wchar_t wbuf[8];
            MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 8);
            wchar_t volname[MAX_PATH] = {0};
            DWORD vsn = 0, mlen = 0, fsf = 0;
            wchar_t fsname[64] = {0};
            GetVolumeInformationW(wbuf, volname, MAX_PATH, &vsn, &mlen, &fsf, fsname, 64);
            char *label;
            if (volname[0]) {
                char *vu = wcs_to_utf8(volname);
                size_t needed = strlen(buf) + (vu ? strlen(vu) : 0) + 8;
                label = (char *)malloc(needed);
                if (label) snprintf(label, needed, "%s  %s", buf, vu ? vu : "");
                free(vu);
            } else {
                label = xstrdup(buf);
            }
            PUSH(xstrdup(buf), label);
        }
    }
    char *home = fs_home();
    if (home) PUSH(home, xstrdup("Home"));
    {
        wchar_t buf[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, buf) == S_OK) {
            char *u = wcs_to_utf8(buf);
            if (u) PUSH(u, xstrdup("Desktop"));
        }
        if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, buf) == S_OK) {
            char *u = wcs_to_utf8(buf);
            if (u) PUSH(u, xstrdup("Documents"));
        }
    }
#else
    PUSH(xstrdup("/"), xstrdup("/  Root"));
    char *home = fs_home();
    if (home) {
        size_t hl = strlen(home);
        char *lbl = (char *)malloc(hl + 8);
        if (lbl) { snprintf(lbl, hl + 8, "~  %s", home); }
        PUSH(home, lbl ? lbl : xstrdup("~"));
    }
    PUSH(xstrdup("/tmp"), xstrdup("/tmp"));

    const char *bases[] = {"/Volumes", "/mnt", "/media", "/run/media", NULL};
    for (int b = 0; bases[b]; b++) {
        DIR *d = opendir(bases[b]);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            char full[4096];
            snprintf(full, sizeof(full), "%s/%s", bases[b], de->d_name);
            if (!fs_is_dir(full)) continue;
            PUSH(xstrdup(full), xstrdup(full));
        }
        closedir(d);
    }
#endif

#undef PUSH
    *out_paths = paths;
    *out_labels = labels;
    *out_count = n;
    return 0;
fail:
    for (size_t i = 0; i < n; i++) { free(paths[i]); free(labels[i]); }
    free(paths); free(labels);
    return -1;
}

void fs_free_locations(char **paths, char **labels, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(paths[i]);
        free(labels[i]);
    }
    free(paths);
    free(labels);
}

static int icmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int cmp_name_dirs_first(const void *pa, const void *pb) {
    const fs_entry *a = (const fs_entry *)pa;
    const fs_entry *b = (const fs_entry *)pb;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return icmp(a->name, b->name);
}

static int cmp_name(const void *pa, const void *pb) {
    const fs_entry *a = (const fs_entry *)pa;
    const fs_entry *b = (const fs_entry *)pb;
    return icmp(a->name, b->name);
}

static int cmp_size_desc(const void *pa, const void *pb) {
    const fs_entry *a = (const fs_entry *)pa;
    const fs_entry *b = (const fs_entry *)pb;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    if (a->size > b->size) return -1;
    if (a->size < b->size) return 1;
    return icmp(a->name, b->name);
}

void fs_sort_entries(fs_entry *entries, size_t count, const char *mode) {
    if (!entries || count == 0) return;
    int (*fn)(const void *, const void *) = cmp_name_dirs_first;
    if (mode) {
        if (strcmp(mode, "name") == 0) fn = cmp_name;
        else if (strcmp(mode, "size") == 0) fn = cmp_size_desc;
        else if (strcmp(mode, "dirs_first") == 0) fn = cmp_name_dirs_first;
    }
    qsort(entries, count, sizeof(fs_entry), fn);
}

char *fs_exe_path(void) {
#ifdef _WIN32
    wchar_t buf[1024];
    DWORD n = GetModuleFileNameW(NULL, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return NULL;
    return wcs_to_utf8(buf);
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

char *fs_exe_dir(void) {
    char *p = fs_exe_path();
    if (!p) return NULL;
    char *dir = fs_parent(p);
    free(p);
    return dir;
}

void fs_open_in_file_manager(const char *path) {
    if (!path || !*path) return;
#ifdef _WIN32
    wchar_t *w = utf8_to_wcs(path);
    if (!w) return;
    ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
#else
    pid_t pid = fork();
    if (pid == 0) {
#  ifdef __APPLE__
        execlp("open", "open", path, (char *)NULL);
#  else
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
#  endif
        _exit(127);
    }
#endif
}

int fs_mkdirs(const char *path) {
#ifdef _WIN32
    wchar_t *w = utf8_to_wcs(path);
    if (!w) return -1;
    int len = (int)wcslen(w);
    for (int i = 1; i < len; i++) {
        if (w[i] == L'\\' || w[i] == L'/') {
            wchar_t c = w[i];
            w[i] = 0;
            CreateDirectoryW(w, NULL);
            w[i] = c;
        }
    }
    BOOL ok = CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
    free(w);
    return ok ? 0 : -1;
#else
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", path);
    size_t l = strlen(buf);
    for (size_t i = 1; i < l; i++) {
        if (buf[i] == '/') {
            buf[i] = 0;
            mkdir(buf, 0755);
            buf[i] = '/';
        }
    }
    return mkdir(buf, 0755) == 0 || errno == EEXIST ? 0 : -1;
#endif
}
