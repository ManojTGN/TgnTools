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
