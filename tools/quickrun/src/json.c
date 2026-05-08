#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
    char *err;
    size_t err_sz;
    int failed;
} parser;

static void set_err(parser *p, const char *msg) {
    if (p->failed) return;
    p->failed = 1;
    if (p->err && p->err_sz > 0) {
        snprintf(p->err, p->err_sz, "%s at offset %zu", msg, p->pos);
    }
}

static void skip_ws(parser *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { p->pos++; continue; }
        if (c == '/' && p->pos + 1 < p->len) {
            if (p->src[p->pos + 1] == '/') {
                p->pos += 2;
                while (p->pos < p->len && p->src[p->pos] != '\n') p->pos++;
                continue;
            }
            if (p->src[p->pos + 1] == '*') {
                p->pos += 2;
                while (p->pos + 1 < p->len && !(p->src[p->pos] == '*' && p->src[p->pos + 1] == '/')) p->pos++;
                if (p->pos + 1 < p->len) p->pos += 2;
                continue;
            }
        }
        break;
    }
}

static int peek(parser *p) { return p->pos < p->len ? (unsigned char)p->src[p->pos] : -1; }
static int advance(parser *p) { return p->pos < p->len ? (unsigned char)p->src[p->pos++] : -1; }

static json_value *make_value(json_type t) {
    json_value *v = (json_value *)calloc(1, sizeof(json_value));
    if (!v) return NULL;
    v->type = t;
    return v;
}

static json_value *parse_value(parser *p);

static int hex_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static void enc_utf8(char *out, size_t *off, size_t cap, unsigned cp) {
    if (cp <= 0x7f) {
        if (*off + 1 > cap) return;
        out[(*off)++] = (char)cp;
    } else if (cp <= 0x7ff) {
        if (*off + 2 > cap) return;
        out[(*off)++] = (char)(0xc0 | (cp >> 6));
        out[(*off)++] = (char)(0x80 | (cp & 0x3f));
    } else if (cp <= 0xffff) {
        if (*off + 3 > cap) return;
        out[(*off)++] = (char)(0xe0 | (cp >> 12));
        out[(*off)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[(*off)++] = (char)(0x80 | (cp & 0x3f));
    } else {
        if (*off + 4 > cap) return;
        out[(*off)++] = (char)(0xf0 | (cp >> 18));
        out[(*off)++] = (char)(0x80 | ((cp >> 12) & 0x3f));
        out[(*off)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[(*off)++] = (char)(0x80 | (cp & 0x3f));
    }
}

static char *parse_string_raw(parser *p) {
    if (advance(p) != '"') { set_err(p, "expected string"); return NULL; }
    size_t start = p->pos;
    size_t cap = 32;
    char *buf = (char *)malloc(cap);
    if (!buf) { set_err(p, "oom"); return NULL; }
    size_t off = 0;
    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->src[p->pos];
        if (c == '"') { p->pos++; buf[off] = 0; (void)start; return buf; }
        if (c == '\\') {
            p->pos++;
            if (p->pos >= p->len) { set_err(p, "bad escape"); free(buf); return NULL; }
            char e = p->src[p->pos++];
            char ec = 0;
            switch (e) {
                case '"': ec = '"'; break;
                case '\\': ec = '\\'; break;
                case '/': ec = '/'; break;
                case 'b': ec = '\b'; break;
                case 'f': ec = '\f'; break;
                case 'n': ec = '\n'; break;
                case 'r': ec = '\r'; break;
                case 't': ec = '\t'; break;
                case 'u': {
                    if (p->pos + 4 > p->len) { set_err(p, "bad \\u"); free(buf); return NULL; }
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        int h = hex_val((unsigned char)p->src[p->pos++]);
                        if (h < 0) { set_err(p, "bad \\u"); free(buf); return NULL; }
                        cp = (cp << 4) | (unsigned)h;
                    }
                    if (off + 4 >= cap) {
                        cap = cap * 2 + 4;
                        char *nb = (char *)realloc(buf, cap);
                        if (!nb) { set_err(p, "oom"); free(buf); return NULL; }
                        buf = nb;
                    }
                    enc_utf8(buf, &off, cap, cp);
                    continue;
                }
                default: set_err(p, "bad escape"); free(buf); return NULL;
            }
            if (off + 1 >= cap) {
                cap *= 2;
                char *nb = (char *)realloc(buf, cap);
                if (!nb) { set_err(p, "oom"); free(buf); return NULL; }
                buf = nb;
            }
            buf[off++] = ec;
            continue;
        }
        if (c < 0x20) { set_err(p, "ctrl in string"); free(buf); return NULL; }
        if (off + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { set_err(p, "oom"); free(buf); return NULL; }
            buf = nb;
        }
        buf[off++] = (char)c;
        p->pos++;
    }
    set_err(p, "unterminated string");
    free(buf);
    return NULL;
}

static json_value *parse_string(parser *p) {
    char *s = parse_string_raw(p);
    if (!s) return NULL;
    json_value *v = make_value(JSON_STRING);
    if (!v) { free(s); set_err(p, "oom"); return NULL; }
    v->v.string = s;
    return v;
}

static json_value *parse_number(parser *p) {
    size_t start = p->pos;
    if (peek(p) == '-') p->pos++;
    while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
    if (p->pos < p->len && p->src[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }
    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-')) p->pos++;
        while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }
    if (p->pos == start) { set_err(p, "bad number"); return NULL; }
    char tmp[64];
    size_t l = p->pos - start;
    if (l >= sizeof(tmp)) l = sizeof(tmp) - 1;
    memcpy(tmp, p->src + start, l);
    tmp[l] = 0;
    json_value *v = make_value(JSON_NUMBER);
    if (!v) { set_err(p, "oom"); return NULL; }
    v->v.number = strtod(tmp, NULL);
    return v;
}

static json_value *parse_literal(parser *p, const char *kw, json_type t, int boolv) {
    size_t l = strlen(kw);
    if (p->pos + l > p->len || strncmp(p->src + p->pos, kw, l) != 0) { set_err(p, "bad literal"); return NULL; }
    p->pos += l;
    json_value *v = make_value(t);
    if (!v) { set_err(p, "oom"); return NULL; }
    if (t == JSON_BOOL) v->v.boolean = boolv;
    return v;
}

static json_value *parse_array(parser *p) {
    if (advance(p) != '[') { set_err(p, "expected ["); return NULL; }
    json_value *arr = make_value(JSON_ARRAY);
    if (!arr) { set_err(p, "oom"); return NULL; }
    size_t cap = 8;
    arr->v.array.items = (json_value **)calloc(cap, sizeof(json_value *));
    if (!arr->v.array.items) { set_err(p, "oom"); free(arr); return NULL; }
    skip_ws(p);
    if (peek(p) == ']') { p->pos++; return arr; }
    while (1) {
        skip_ws(p);
        if (peek(p) == ']') { p->pos++; break; } /* tolerate trailing comma */
        json_value *e = parse_value(p);
        if (!e) { json_free(arr); return NULL; }
        if (arr->v.array.count >= cap) {
            cap *= 2;
            json_value **na = (json_value **)realloc(arr->v.array.items, cap * sizeof(json_value *));
            if (!na) { set_err(p, "oom"); json_free(e); json_free(arr); return NULL; }
            arr->v.array.items = na;
        }
        arr->v.array.items[arr->v.array.count++] = e;
        skip_ws(p);
        int c = peek(p);
        if (c == ',') { p->pos++; continue; }
        if (c == ']') { p->pos++; break; }
        set_err(p, "expected , or ]");
        json_free(arr);
        return NULL;
    }
    return arr;
}

static json_value *parse_object(parser *p) {
    if (advance(p) != '{') { set_err(p, "expected {"); return NULL; }
    json_value *obj = make_value(JSON_OBJECT);
    if (!obj) { set_err(p, "oom"); return NULL; }
    size_t cap = 8;
    obj->v.object.keys = (char **)calloc(cap, sizeof(char *));
    obj->v.object.values = (json_value **)calloc(cap, sizeof(json_value *));
    if (!obj->v.object.keys || !obj->v.object.values) { set_err(p, "oom"); json_free(obj); return NULL; }
    skip_ws(p);
    if (peek(p) == '}') { p->pos++; return obj; }
    while (1) {
        skip_ws(p);
        if (peek(p) == '}') { p->pos++; break; } /* tolerate trailing comma */
        if (peek(p) != '"') { set_err(p, "expected key string"); json_free(obj); return NULL; }
        char *key = parse_string_raw(p);
        if (!key) { json_free(obj); return NULL; }
        skip_ws(p);
        if (advance(p) != ':') { set_err(p, "expected :"); free(key); json_free(obj); return NULL; }
        skip_ws(p);
        json_value *val = parse_value(p);
        if (!val) { free(key); json_free(obj); return NULL; }
        if (obj->v.object.count >= cap) {
            cap *= 2;
            char **nk = (char **)realloc(obj->v.object.keys, cap * sizeof(char *));
            json_value **nv = (json_value **)realloc(obj->v.object.values, cap * sizeof(json_value *));
            if (!nk || !nv) { set_err(p, "oom"); free(key); json_free(val); json_free(obj); return NULL; }
            obj->v.object.keys = nk;
            obj->v.object.values = nv;
        }
        obj->v.object.keys[obj->v.object.count] = key;
        obj->v.object.values[obj->v.object.count] = val;
        obj->v.object.count++;
        skip_ws(p);
        int c = peek(p);
        if (c == ',') { p->pos++; continue; }
        if (c == '}') { p->pos++; break; }
        set_err(p, "expected , or }");
        json_free(obj);
        return NULL;
    }
    return obj;
}

static json_value *parse_value(parser *p) {
    skip_ws(p);
    int c = peek(p);
    if (c < 0) { set_err(p, "unexpected EOF"); return NULL; }
    if (c == '"') return parse_string(p);
    if (c == '{') return parse_object(p);
    if (c == '[') return parse_array(p);
    if (c == 't') return parse_literal(p, "true", JSON_BOOL, 1);
    if (c == 'f') return parse_literal(p, "false", JSON_BOOL, 0);
    if (c == 'n') return parse_literal(p, "null", JSON_NULL, 0);
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(p);
    set_err(p, "unexpected character");
    return NULL;
}

json_value *json_parse(const char *src, char *err, size_t err_sz) {
    parser p;
    memset(&p, 0, sizeof(p));
    p.src = src ? src : "";
    p.len = strlen(p.src);
    p.err = err;
    p.err_sz = err_sz;
    if (err && err_sz > 0) err[0] = 0;
    skip_ws(&p);
    json_value *v = parse_value(&p);
    if (!v) return NULL;
    skip_ws(&p);
    if (p.pos != p.len && !p.failed) {
        if (err && err_sz > 0) snprintf(err, err_sz, "trailing content at offset %zu", p.pos);
        json_free(v);
        return NULL;
    }
    return v;
}

void json_free(json_value *j) {
    if (!j) return;
    switch (j->type) {
        case JSON_STRING: free(j->v.string); break;
        case JSON_ARRAY:
            for (size_t i = 0; i < j->v.array.count; i++) json_free(j->v.array.items[i]);
            free(j->v.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < j->v.object.count; i++) {
                free(j->v.object.keys[i]);
                json_free(j->v.object.values[i]);
            }
            free(j->v.object.keys);
            free(j->v.object.values);
            break;
        default: break;
    }
    free(j);
}

json_value *json_get(json_value *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->v.object.count; i++) {
        if (strcmp(obj->v.object.keys[i], key) == 0) return obj->v.object.values[i];
    }
    return NULL;
}

const char *json_string(const json_value *j) {
    if (!j || j->type != JSON_STRING) return NULL;
    return j->v.string;
}

double json_number(const json_value *j) {
    if (!j) return 0.0;
    if (j->type == JSON_NUMBER) return j->v.number;
    if (j->type == JSON_BOOL) return j->v.boolean ? 1.0 : 0.0;
    return 0.0;
}

int json_bool(const json_value *j) {
    if (!j) return 0;
    if (j->type == JSON_BOOL) return j->v.boolean;
    if (j->type == JSON_NUMBER) return j->v.number != 0.0 ? 1 : 0;
    return 0;
}

size_t json_array_len(const json_value *j) {
    if (!j || j->type != JSON_ARRAY) return 0;
    return j->v.array.count;
}

json_value *json_array_at(const json_value *j, size_t i) {
    if (!j || j->type != JSON_ARRAY || i >= j->v.array.count) return NULL;
    return j->v.array.items[i];
}

const char *json_get_string(json_value *obj, const char *key, const char *fallback) {
    json_value *v = json_get(obj, key);
    const char *s = json_string(v);
    return s ? s : fallback;
}

double json_get_number(json_value *obj, const char *key, double fallback) {
    json_value *v = json_get(obj, key);
    if (!v) return fallback;
    if (v->type != JSON_NUMBER && v->type != JSON_BOOL) return fallback;
    return json_number(v);
}

int json_get_bool(json_value *obj, const char *key, int fallback) {
    json_value *v = json_get(obj, key);
    if (!v) return fallback;
    if (v->type != JSON_BOOL && v->type != JSON_NUMBER) return fallback;
    return json_bool(v);
}

json_value *json_make_string(const char *s) {
    json_value *v = (json_value *)calloc(1, sizeof(json_value));
    if (!v) return NULL;
    v->type = JSON_STRING;
    size_t len = s ? strlen(s) : 0;
    v->v.string = (char *)malloc(len + 1);
    if (!v->v.string) { free(v); return NULL; }
    if (s) memcpy(v->v.string, s, len);
    v->v.string[len] = 0;
    return v;
}

json_value *json_make_object(void) {
    json_value *v = (json_value *)calloc(1, sizeof(json_value));
    if (!v) return NULL;
    v->type = JSON_OBJECT;
    return v;
}

int json_object_set(json_value *obj, const char *key, json_value *value) {
    if (!obj || obj->type != JSON_OBJECT || !key || !value) return -1;

    for (size_t i = 0; i < obj->v.object.count; i++) {
        if (strcmp(obj->v.object.keys[i], key) == 0) {
            json_free(obj->v.object.values[i]);
            obj->v.object.values[i] = value;
            return 0;
        }
    }

    size_t new_count = obj->v.object.count + 1;
    char **keys = (char **)realloc(obj->v.object.keys, new_count * sizeof(char *));
    if (!keys) return -1;
    obj->v.object.keys = keys;

    json_value **values = (json_value **)realloc(obj->v.object.values, new_count * sizeof(json_value *));
    if (!values) return -1;
    obj->v.object.values = values;

    size_t key_len = strlen(key);
    char *key_copy = (char *)malloc(key_len + 1);
    if (!key_copy) return -1;
    memcpy(key_copy, key, key_len + 1);

    obj->v.object.keys[obj->v.object.count] = key_copy;
    obj->v.object.values[obj->v.object.count] = value;
    obj->v.object.count = new_count;
    return 0;
}

static void json_write_indent(FILE *f, int level) {
    for (int i = 0; i < level; i++) fputs("    ", f);
}

static void json_write_string_escaped(FILE *f, const char *s) {
    fputc('"', f);
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            unsigned char c = *p;
            switch (c) {
                case '"':  fputs("\\\"", f); break;
                case '\\': fputs("\\\\", f); break;
                case '\b': fputs("\\b", f);  break;
                case '\f': fputs("\\f", f);  break;
                case '\n': fputs("\\n", f);  break;
                case '\r': fputs("\\r", f);  break;
                case '\t': fputs("\\t", f);  break;
                default:
                    if (c < 0x20) fprintf(f, "\\u%04x", c);
                    else fputc(c, f);
                    break;
            }
        }
    }
    fputc('"', f);
}

static void json_write_value(FILE *f, const json_value *v, int level) {
    if (!v) { fputs("null", f); return; }
    switch (v->type) {
        case JSON_NULL:   fputs("null", f); return;
        case JSON_BOOL:   fputs(v->v.boolean ? "true" : "false", f); return;
        case JSON_NUMBER: {
            double n = v->v.number;
            if (n == (double)(long long)n) fprintf(f, "%lld", (long long)n);
            else                           fprintf(f, "%g", n);
            return;
        }
        case JSON_STRING: json_write_string_escaped(f, v->v.string); return;
        case JSON_ARRAY:
            if (v->v.array.count == 0) { fputs("[]", f); return; }
            fputs("[\n", f);
            for (size_t i = 0; i < v->v.array.count; i++) {
                json_write_indent(f, level + 1);
                json_write_value(f, v->v.array.items[i], level + 1);
                if (i + 1 < v->v.array.count) fputc(',', f);
                fputc('\n', f);
            }
            json_write_indent(f, level);
            fputc(']', f);
            return;
        case JSON_OBJECT:
            if (v->v.object.count == 0) { fputs("{}", f); return; }
            fputs("{\n", f);
            for (size_t i = 0; i < v->v.object.count; i++) {
                json_write_indent(f, level + 1);
                json_write_string_escaped(f, v->v.object.keys[i]);
                fputs(": ", f);
                json_write_value(f, v->v.object.values[i], level + 1);
                if (i + 1 < v->v.object.count) fputc(',', f);
                fputc('\n', f);
            }
            json_write_indent(f, level);
            fputc('}', f);
            return;
    }
}

int json_write_to_file(const json_value *root, const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    json_write_value(f, root, 0);
    fputc('\n', f);
    fclose(f);
    return 0;
}
