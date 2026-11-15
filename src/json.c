#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---------------- 构造 ---------------- */
static JVal *jnew(JType t) {
    JVal *v = (JVal *)xcalloc(1, sizeof(JVal));
    v->type = t;
    return v;
}
JVal *json_new_obj(void) { return jnew(J_OBJ); }
JVal *json_new_arr(void) { return jnew(J_ARR); }
JVal *json_new_str(const char *s) { JVal *v = jnew(J_STR); v->str = xstrdup(s ? s : ""); return v; }
JVal *json_new_num(double d) { JVal *v = jnew(J_NUM); v->num = d; return v; }
JVal *json_new_bool(int b) { JVal *v = jnew(J_BOOL); v->boolean = b ? 1 : 0; return v; }

void json_free(JVal *v) {
    if (!v) return;
    free(v->str);
    for (int i = 0; i < v->nitems; i++) json_free(v->items[i]);
    free(v->items);
    for (int i = 0; i < v->nkeys; i++) { free(v->keys[i]); json_free(v->vals[i]); }
    free(v->keys);
    free(v->vals);
    free(v);
}

/* ---------------- 解析 ---------------- */
typedef struct {
    const char *p;
    int error;
    int depth;
} P;

static void skip_ws(P *ps) {
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r' || *ps->p == '\n') ps->p++;
}

static JVal *parse_value(P *ps);

static void append_utf8(StrBuf *sb, u32 cp) {
    char tmp[8];
    size_t n = utf8_encode_one(cp, tmp);
    sb_append_n(sb, tmp, n);
}

static int parse_hex4(const char *p, u32 *out) {
    u32 v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (u32)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (u32)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

static char *parse_string_raw(P *ps) {
    if (*ps->p != '"') { ps->error = 1; return NULL; }
    ps->p++;
    StrBuf sb; sb_init(&sb);
    while (*ps->p && *ps->p != '"') {
        char c = *ps->p;
        if (c == '\\') {
            ps->p++;
            char e = *ps->p;
            if (!e) { ps->error = 1; break; }
            ps->p++;
            switch (e) {
            case '"': sb_append(&sb, "\""); break;
            case '\\': sb_append(&sb, "\\"); break;
            case '/': sb_append(&sb, "/"); break;
            case 'b': sb_append(&sb, "\b"); break;
            case 'f': sb_append(&sb, "\f"); break;
            case 'n': sb_append(&sb, "\n"); break;
            case 'r': sb_append(&sb, "\r"); break;
            case 't': sb_append(&sb, "\t"); break;
            case 'u': {
                u32 cp = 0;
                if (!parse_hex4(ps->p, &cp)) { ps->error = 1; break; }
                ps->p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && ps->p[0] == '\\' && ps->p[1] == 'u') {
                    u32 lo = 0;
                    if (parse_hex4(ps->p + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        ps->p += 6;
                    }
                }
                append_utf8(&sb, cp);
                break;
            }
            default: ps->error = 1; break;
            }
            if (ps->error) break;
        } else if ((unsigned char)c < 0x20) {
            ps->error = 1; break;
        } else {
            sb_append_n(&sb, ps->p, 1);
            ps->p++;
        }
    }
    if (*ps->p != '"') { ps->error = 1; sb_free(&sb); return NULL; }
    ps->p++;
    return sb_detach(&sb);
}

static JVal *parse_array(P *ps) {
    JVal *arr = json_new_arr();
    ps->p++; /* [ */
    skip_ws(ps);
    if (*ps->p == ']') { ps->p++; return arr; }
    while (*ps->p) {
        JVal *v = parse_value(ps);
        if (ps->error || !v) { json_free(v); json_free(arr); ps->error = 1; return NULL; }
        json_add(arr, v);
        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; skip_ws(ps); continue; }
        if (*ps->p == ']') { ps->p++; return arr; }
        ps->error = 1; break;
    }
    json_free(arr);
    return NULL;
}

static JVal *parse_object(P *ps) {
    JVal *obj = json_new_obj();
    ps->p++; /* { */
    skip_ws(ps);
    if (*ps->p == '}') { ps->p++; return obj; }
    while (*ps->p) {
        skip_ws(ps);
        char *key = parse_string_raw(ps);
        if (!key) { json_free(obj); ps->error = 1; return NULL; }
        skip_ws(ps);
        if (*ps->p != ':') { free(key); json_free(obj); ps->error = 1; return NULL; }
        ps->p++;
        skip_ws(ps);
        JVal *v = parse_value(ps);
        if (!v || ps->error) { free(key); json_free(v); json_free(obj); ps->error = 1; return NULL; }
        json_set(obj, key, v);
        free(key);
        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; return obj; }
        ps->error = 1; break;
    }
    json_free(obj);
    return NULL;
}

static JVal *parse_value(P *ps) {
    skip_ws(ps);
    if (ps->depth++ > 64) { ps->error = 1; return NULL; }
    JVal *r = NULL;
    if (*ps->p == '{') r = parse_object(ps);
    else if (*ps->p == '[') r = parse_array(ps);
    else if (*ps->p == '"') { char *s = parse_string_raw(ps); r = s ? json_new_str(s) : NULL; free(s); }
    else if (strncmp(ps->p, "true", 4) == 0) { ps->p += 4; r = json_new_bool(1); }
    else if (strncmp(ps->p, "false", 5) == 0) { ps->p += 5; r = json_new_bool(0); }
    else if (strncmp(ps->p, "null", 4) == 0) { ps->p += 4; r = jnew(J_NULL); }
    else if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) {
        char *end = NULL;
        double d = strtod(ps->p, &end);
        if (end == ps->p) { ps->error = 1; return NULL; }
        ps->p = end;
        r = json_new_num(d);
    } else if (*ps->p == 0) {
        ps->error = 1;
    } else {
        ps->error = 1;
    }
    ps->depth--;
    return r;
}

JVal *json_parse(const char *text) {
    if (!text) return NULL;
    P ps; ps.p = text; ps.error = 0; ps.depth = 0;
    JVal *v = parse_value(&ps);
    if (ps.error) { json_free(v); return NULL; }
    return v;
}

/* ---------------- 访问 ---------------- */
JVal *json_get(JVal *obj, const char *key) {
    if (!obj || obj->type != J_OBJ || !key) return NULL;
    for (int i = 0; i < obj->nkeys; i++) {
        if (strcmp(obj->keys[i], key) == 0) return obj->vals[i];
    }
    return NULL;
}
const char *json_str(JVal *obj, const char *key, const char *def) {
    JVal *v = json_get(obj, key);
    if (v && v->type == J_STR) return v->str;
    return def;
}
int json_int(JVal *obj, const char *key, int def) {
    JVal *v = json_get(obj, key);
    if (v && v->type == J_NUM) return (int)v->num;
    if (v && v->type == J_BOOL) return v->boolean;
    return def;
}
double json_num(JVal *obj, const char *key, double def) {
    JVal *v = json_get(obj, key);
    if (v && v->type == J_NUM) return v->num;
    return def;
}
int json_bool(JVal *obj, const char *key, int def) {
    JVal *v = json_get(obj, key);
    if (v && v->type == J_BOOL) return v->boolean;
    if (v && v->type == J_NUM) return v->num != 0;
    return def;
}

/* ---------------- 修改 ---------------- */
void json_set(JVal *obj, const char *key, JVal *val) {
    if (!obj || obj->type != J_OBJ || !val) { json_free(val); return; }
    for (int i = 0; i < obj->nkeys; i++) {
        if (strcmp(obj->keys[i], key) == 0) {
            json_free(obj->vals[i]);
            obj->vals[i] = val;
            return;
        }
    }
    if (obj->nkeys == obj->ckeys) {
        obj->ckeys = obj->ckeys ? obj->ckeys * 2 : 8;
        obj->keys = (char **)xrealloc(obj->keys, (size_t)obj->ckeys * sizeof(char *));
        obj->vals = (JVal **)xrealloc(obj->vals, (size_t)obj->ckeys * sizeof(JVal *));
    }
    obj->keys[obj->nkeys] = xstrdup(key);
    obj->vals[obj->nkeys] = val;
    obj->nkeys++;
}
void json_set_str(JVal *obj, const char *key, const char *s) { json_set(obj, key, json_new_str(s)); }
void json_set_int(JVal *obj, const char *key, int v) { json_set(obj, key, json_new_num((double)v)); }
void json_set_bool(JVal *obj, const char *key, int v) { json_set(obj, key, json_new_bool(v)); }

void json_add(JVal *arr, JVal *val) {
    if (!arr || arr->type != J_ARR || !val) { json_free(val); return; }
    if (arr->nitems == arr->citems) {
        arr->citems = arr->citems ? arr->citems * 2 : 8;
        arr->items = (JVal **)xrealloc(arr->items, (size_t)arr->citems * sizeof(JVal *));
    }
    arr->items[arr->nitems++] = val;
}

/* ---------------- 序列化 ---------------- */
static void dump_string(StrBuf *sb, const char *s) {
    sb_append(sb, "\"");
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"': sb_append(sb, "\\\""); break;
        case '\\': sb_append(sb, "\\\\"); break;
        case '\n': sb_append(sb, "\\n"); break;
        case '\r': sb_append(sb, "\\r"); break;
        case '\t': sb_append(sb, "\\t"); break;
        case '\b': sb_append(sb, "\\b"); break;
        case '\f': sb_append(sb, "\\f"); break;
        default:
            if (c < 0x20 || c == 0x7F) {
                char tmp[8];
                _snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                sb_append(sb, tmp);
            } else {
                sb_append_n(sb, p, 1);
            }
        }
    }
    sb_append(sb, "\"");
}

static void dump_value(StrBuf *sb, JVal *v, int pretty, int indent) {
    if (!v) { sb_append(sb, "null"); return; }
    switch (v->type) {
    case J_NULL: sb_append(sb, "null"); break;
    case J_BOOL: sb_append(sb, v->boolean ? "true" : "false"); break;
    case J_NUM: {
        char tmp[64];
        if (v->num == (double)(long long)v->num) _snprintf(tmp, sizeof(tmp), "%lld", (long long)v->num);
        else _snprintf(tmp, sizeof(tmp), "%.10g", v->num);
        tmp[63] = 0;
        sb_append(sb, tmp);
        break;
    }
    case J_STR: dump_string(sb, v->str ? v->str : ""); break;
    case J_ARR:
        sb_append(sb, "[");
        for (int i = 0; i < v->nitems; i++) {
            if (i) sb_append(sb, ",");
            if (pretty) { sb_append(sb, "\n"); for (int k = 0; k < indent + 1; k++) sb_append(sb, "  "); }
            dump_value(sb, v->items[i], pretty, indent + 1);
        }
        if (pretty && v->nitems) { sb_append(sb, "\n"); for (int k = 0; k < indent; k++) sb_append(sb, "  "); }
        sb_append(sb, "]");
        break;
    case J_OBJ:
        sb_append(sb, "{");
        for (int i = 0; i < v->nkeys; i++) {
            if (i) sb_append(sb, ",");
            if (pretty) { sb_append(sb, "\n"); for (int k = 0; k < indent + 1; k++) sb_append(sb, "  "); }
            dump_string(sb, v->keys[i]);
            sb_append(sb, pretty ? ": " : ":");
            dump_value(sb, v->vals[i], pretty, indent + 1);
        }
        if (pretty && v->nkeys) { sb_append(sb, "\n"); for (int k = 0; k < indent; k++) sb_append(sb, "  "); }
        sb_append(sb, "}");
        break;
    }
}

char *json_dump(JVal *v, int pretty) {
    StrBuf sb; sb_init(&sb);
    dump_value(&sb, v, pretty, 0);
    return sb_detach(&sb);
}
