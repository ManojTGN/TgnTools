#ifndef QUICKRUN_CONFIG_H
#define QUICKRUN_CONFIG_H

#include <stddef.h>

#include "action.h"
#include "keyspec.h"

typedef struct {
    keyspec key;
    action  act;
} binding;

typedef struct {
    char    *log_file;
    binding *bindings;
    size_t   binding_count;
} quickrun_config;

quickrun_config *quickrun_config_load(const char *override_path, char *err, size_t err_sz);
void             quickrun_config_free(quickrun_config *c);
char            *quickrun_config_resolve_path(const char *override_path);
char            *quickrun_default_log_path(void);

#endif
