#ifndef TCD_FS_H
#define TCD_FS_H

#include <stddef.h>

#ifdef _WIN32
#  define TCD_PATH_SEP '\\'
#  define TCD_PATH_SEP_STR "\\"
#else
#  define TCD_PATH_SEP '/'
#  define TCD_PATH_SEP_STR "/"
#endif

typedef struct {
    char *name;
    int is_dir;
    long long size;
} fs_entry;

int fs_list(const char *path, int show_hidden, fs_entry **out_entries, size_t *out_count);
void fs_free_list(fs_entry *entries, size_t count);

char *fs_cwd(void);
char *fs_home(void);
char *fs_join(const char *base, const char *child);
char *fs_parent(const char *path);
char *fs_normalize(const char *path);
int fs_is_dir(const char *path);
int fs_exists(const char *path);

int fs_list_locations(char ***out_paths, char ***out_labels, size_t *out_count);
void fs_free_locations(char **paths, char **labels, size_t count);

void fs_sort_entries(fs_entry *entries, size_t count, const char *mode);

int fs_mkdirs(const char *path);

void fs_open_in_file_manager(const char *path);

char *fs_exe_path(void);
char *fs_exe_dir(void);

#endif
