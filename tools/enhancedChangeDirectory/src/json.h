#ifndef TCD_JSON_H
#define TCD_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type;

typedef struct json_value {
    json_type type;
    union {
        int boolean;
        double number;
        char *string;
        struct {
            struct json_value **items;
            size_t count;
        } array;
        struct {
            char **keys;
            struct json_value **values;
            size_t count;
        } object;
    } v;
} json_value;

json_value *json_parse(const char *src, char *err, size_t err_sz);
void json_free(json_value *j);

json_value *json_get(json_value *obj, const char *key);
const char *json_string(const json_value *j);
double json_number(const json_value *j);
int json_bool(const json_value *j);
size_t json_array_len(const json_value *j);
json_value *json_array_at(const json_value *j, size_t i);

const char *json_get_string(json_value *obj, const char *key, const char *fallback);
double json_get_number(json_value *obj, const char *key, double fallback);
int json_get_bool(json_value *obj, const char *key, int fallback);

#endif
