/*
 * json.h — minimal JSON builder and pull-parser
 *
 * Builder:  append-only into an Arena, no malloc, no escaping surprises
 * Parser:   recursive-descent over a flat char buffer (JSON.h compatible API)
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * MIT License
 */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "arena.h"
#include "sv.h"

/* ── JSON token types ──────────────────────────────────────────────────── */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
    JSON_ERROR,
} JsonType;

typedef struct JsonVal JsonVal;
typedef struct JsonPair JsonPair;

struct JsonPair {
    Sv       key;
    JsonVal *val;
    JsonPair *next;
};

struct JsonVal {
    JsonType type;
    union {
        bool        b;
        double      n;
        Sv          s;           /* STRING: points into source buffer */
        JsonVal    *items;       /* ARRAY:  linked list via ->next     */
        JsonPair   *pairs;       /* OBJECT: linked list via ->next     */
    } u;
    JsonVal *next;               /* sibling in array/object            */
};

/* ── Builder ────────────────────────────────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    Arena *a;
} JsonBuf;

static inline JsonBuf jbuf_new(Arena *a, size_t cap)
{
    JsonBuf b = {0};
    b.buf = (char *)arena_alloc(a, cap);
    b.cap = cap;
    b.a   = a;
    return b;
}

static inline void jbuf_raw(JsonBuf *b, const char *s, size_t n)
{
    /* grow into arena if needed — simple: just assert for now */
    if (b->len + n >= b->cap) {
        size_t newcap = (b->cap * 2) + n;
        char  *newbuf = (char *)arena_alloc(b->a, newcap);
        memcpy(newbuf, b->buf, b->len);
        b->buf = newbuf;
        b->cap = newcap;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static inline void jbuf_cstr(JsonBuf *b, const char *s)
{
    jbuf_raw(b, s, strlen(s));
}

/* Append a JSON-escaped string value WITH surrounding quotes */
static inline void jbuf_str(JsonBuf *b, const char *s)
{
    jbuf_raw(b, "\"", 1);
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"':  jbuf_raw(b, "\\\"", 2); break;
        case '\\': jbuf_raw(b, "\\\\", 2); break;
        case '\n': jbuf_raw(b, "\\n",  2); break;
        case '\r': jbuf_raw(b, "\\r",  2); break;
        case '\t': jbuf_raw(b, "\\t",  2); break;
        default:
            if ((unsigned char)*p < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
                jbuf_raw(b, esc, 6);
            } else {
                jbuf_raw(b, p, 1);
            }
        }
    }
    jbuf_raw(b, "\"", 1);
}

static inline void jbuf_sv(JsonBuf *b, Sv s)
{
    /* null-terminate a copy via arena then call jbuf_str */
    char *tmp = arena_strdup(b->a, "");
    (void)tmp; /* ensure arena has working space */
    /* inline for efficiency */
    jbuf_raw(b, "\"", 1);
    for (size_t i = 0; i < s.len; i++) {
        char c = s.data[i];
        if ('"'  == c) { jbuf_raw(b, "\\\"", 2); continue; }
        if ('\\' == c) { jbuf_raw(b, "\\\\", 2); continue; }
        if ('\n' == c) { jbuf_raw(b, "\\n",  2); continue; }
        jbuf_raw(b, &c, 1);
    }
    jbuf_raw(b, "\"", 1);
}

static inline void jbuf_int(JsonBuf *b, long long v)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%lld", v);
    jbuf_cstr(b, tmp);
}

static inline void jbuf_bool(JsonBuf *b, bool v)
{
    jbuf_cstr(b, v ? "true" : "false");
}

static inline void jbuf_null(JsonBuf *b) { jbuf_cstr(b, "null"); }

/* Convenience: "key": */
static inline void jbuf_key(JsonBuf *b, const char *key)
{
    jbuf_str(b, key);
    jbuf_raw(b, ":", 1);
}

static inline Sv jbuf_sv_result(const JsonBuf *b)
{
    return (Sv){ b->buf, b->len };
}

/* ── Parser ─────────────────────────────────────────────────────────────── */

typedef struct {
    const char *src;
    size_t      pos;
    size_t      len;
    Arena      *a;
    char        err[128];
} JsonParser;

static inline JsonParser json_parser_new(const char *src, size_t len, Arena *a)
{
    JsonParser p = {0};
    p.src = src;
    p.len = len;
    p.a   = a;
    return p;
}

static inline void jp_skip_ws(JsonParser *p)
{
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (' ' == c || '\t' == c || '\n' == c || '\r' == c) p->pos++;
        else break;
    }
}

static inline char jp_peek(JsonParser *p) {
    jp_skip_ws(p);
    return p->pos < p->len ? p->src[p->pos] : '\0';
}

static inline char jp_advance(JsonParser *p) {
    return p->pos < p->len ? p->src[p->pos++] : '\0';
}

/* Forward declaration */
static JsonVal *jp_value(JsonParser *p);

static JsonVal *jp_string(JsonParser *p)
{
    jp_skip_ws(p);
    if (p->src[p->pos] != '"') {
        snprintf(p->err, sizeof(p->err), "expected '\"' at pos %zu", p->pos);
        return NULL;
    }
    p->pos++; /* consume opening quote */
    size_t start = p->pos;
    while (p->pos < p->len && p->src[p->pos] != '"') {
        if ('\\' == p->src[p->pos]) p->pos++; /* skip escaped char */
        p->pos++;
    }
    JsonVal *v  = (JsonVal *)arena_alloc(p->a, sizeof *v);
    v->type     = JSON_STRING;
    v->u.s      = (Sv){ p->src + start, p->pos - start };
    p->pos++;   /* consume closing quote */
    return v;
}

static JsonVal *jp_object(JsonParser *p)
{
    p->pos++; /* consume '{' */
    JsonVal  *v     = (JsonVal *)arena_alloc(p->a, sizeof *v);
    v->type         = JSON_OBJECT;
    v->u.pairs      = NULL;
    JsonPair **tail = &v->u.pairs;

    jp_skip_ws(p);
    if ('}' == jp_peek(p)) { p->pos++; return v; }

    do {
        jp_skip_ws(p);
        JsonVal *ks = jp_string(p);
        if (!ks) return NULL;
        jp_skip_ws(p);
        if (':' != jp_advance(p)) {
            snprintf(p->err, sizeof(p->err), "expected ':' at pos %zu", p->pos);
            return NULL;
        }
        JsonVal *val = jp_value(p);
        if (!val) return NULL;
        JsonPair *pair = (JsonPair *)arena_alloc(p->a, sizeof *pair);
        pair->key  = ks->u.s;
        pair->val  = val;
        pair->next = NULL;
        *tail = pair;
        tail  = &pair->next;
        jp_skip_ws(p);
    } while (',' == jp_peek(p) && p->pos++);

    jp_skip_ws(p);
    if ('}' != jp_advance(p)) {
        snprintf(p->err, sizeof(p->err), "expected '}' at pos %zu", p->pos);
        return NULL;
    }
    return v;
}

static JsonVal *jp_array(JsonParser *p)
{
    p->pos++; /* consume '[' */
    JsonVal  *v    = (JsonVal *)arena_alloc(p->a, sizeof *v);
    v->type        = JSON_ARRAY;
    v->u.items     = NULL;
    JsonVal **tail = &v->u.items;

    jp_skip_ws(p);
    if (']' == jp_peek(p)) { p->pos++; return v; }

    do {
        JsonVal *item = jp_value(p);
        if (!item) return NULL;
        item->next = NULL;
        *tail = item;
        tail  = &item->next;
        jp_skip_ws(p);
    } while (',' == jp_peek(p) && p->pos++);

    jp_skip_ws(p);
    if (']' != jp_advance(p)) {
        snprintf(p->err, sizeof(p->err), "expected ']' at pos %zu", p->pos);
        return NULL;
    }
    return v;
}

static JsonVal *jp_number(JsonParser *p)
{
    jp_skip_ws(p);
    size_t start = p->pos;
    if ('-' == p->src[p->pos]) p->pos++;
    while (p->pos < p->len &&
           ((p->src[p->pos] >= '0' && p->src[p->pos] <= '9') ||
            '.' == p->src[p->pos] || 'e' == p->src[p->pos] ||
            'E' == p->src[p->pos] || '+' == p->src[p->pos] ||
            '-' == p->src[p->pos]))
        p->pos++;
    JsonVal *v = (JsonVal *)arena_alloc(p->a, sizeof *v);
    v->type    = JSON_NUMBER;
    char tmp[64] = {0};
    size_t n = p->pos - start;
    if (n >= sizeof tmp) n = sizeof(tmp) - 1;
    memcpy(tmp, p->src + start, n);
    v->u.n = strtod(tmp, NULL);
    return v;
}

static JsonVal *jp_value(JsonParser *p)
{
    jp_skip_ws(p);
    if (p->pos >= p->len) return NULL;
    char c = p->src[p->pos];
    if ('"'  == c) return jp_string(p);
    if ('{'  == c) return jp_object(p);
    if ('['  == c) return jp_array(p);
    if ('-'  == c || (c >= '0' && c <= '9')) return jp_number(p);
    if (0 == strncmp(p->src + p->pos, "true",  4)) {
        p->pos += 4;
        JsonVal *v = (JsonVal *)arena_alloc(p->a, sizeof *v);
        v->type = JSON_BOOL; v->u.b = true; return v;
    }
    if (0 == strncmp(p->src + p->pos, "false", 5)) {
        p->pos += 5;
        JsonVal *v = (JsonVal *)arena_alloc(p->a, sizeof *v);
        v->type = JSON_BOOL; v->u.b = false; return v;
    }
    if (0 == strncmp(p->src + p->pos, "null",  4)) {
        p->pos += 4;
        JsonVal *v = (JsonVal *)arena_alloc(p->a, sizeof *v);
        v->type = JSON_NULL; return v;
    }
    snprintf(p->err, sizeof(p->err), "unexpected char '%c' at pos %zu", c, p->pos);
    return NULL;
}

/* Public entry point */
static inline JsonVal *json_parse(const char *src, size_t len, Arena *a, char *errbuf, size_t errsz)
{
    JsonParser p = json_parser_new(src, len, a);
    JsonVal   *v = jp_value(&p);
    if (NULL == v && errbuf)
        snprintf(errbuf, errsz, "%s", p.err);
    return v;
}

/* Lookup key in JSON object — returns NULL if not found */
static inline JsonVal *json_get(const JsonVal *obj, const char *key)
{
    if (NULL == obj || JSON_OBJECT != obj->type) return NULL;
    Sv k = SV(key);
    for (JsonPair *pr = obj->u.pairs; pr; pr = pr->next)
        if (sv_eq(pr->key, k)) return pr->val;
    return NULL;
}

/* Safe string extraction from a JSON string node */
static inline const char *json_str(const JsonVal *v, Arena *a)
{
    if (NULL == v || JSON_STRING != v->type) return NULL;
    return arena_strdup(a, "") ? /* ensure arena active */
           (char *)memcpy(arena_alloc(a, v->u.s.len + 1), v->u.s.data, v->u.s.len)
           : NULL;
}

static inline long long json_int(const JsonVal *v)
{
    if (NULL == v || JSON_NUMBER != v->type) return 0;
    return (long long)v->u.n;
}

#endif /* JSON_H */
