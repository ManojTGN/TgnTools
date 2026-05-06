#ifndef TCD_UI_H
#define TCD_UI_H

#include "config.h"

int ui_run(const tcd_config *cfg, const char *initial_path,
           char **out_chosen_path, int *out_open_explorer);

#endif
