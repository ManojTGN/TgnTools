#ifndef QUICKRUN_ACTION_H
#define QUICKRUN_ACTION_H

#include <stddef.h>

typedef enum {
    ACTION_RUN,
    ACTION_OPEN
} action_kind;

typedef struct {
    action_kind kind;

    char  *command;        /* for ACTION_RUN: program path or name */
    char **args;           /* for ACTION_RUN: argv tail (NULL-terminated) */
    size_t arg_count;

    char  *target;         /* for ACTION_OPEN: URL or path */
} action;

void action_free(action *a);
int  action_execute(const action *a);

#endif
