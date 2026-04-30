/*
 * json.h — arena-backed JSON builder (JsonBuf)
 *
 * Parser: use sj.h (rxi/sj.h, public domain).
 *   sj_Reader r = sj_reader(buf, len);
 *   sj_Value  v = sj_read(&r);
 *   sj_iter_object(&r, v, &key, &val);
 *   sj_iter_array (&r, v, &val);
 *
 * sheredom/json.h was rejected: it malloc()s internally, breaking
 * the arena-only-in-hot-path security model.
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "arena.h"
#include "sv.h"

/* ── JSON builder (append-only into Arena) ─────────────────────────────── */

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

/* Append a JSON-escaped string WITH surrounding double quotes */
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
                snprintf(esc, sizeof esc, "\\u%04x", (unsigned char)*p);
                jbuf_raw(b, esc, 6);
            } else {
                jbuf_raw(b, p, 1);
            }
        }
    }
    jbuf_raw(b, "\"", 1);
}

/* Append a JSON-escaped string view WITH surrounding double quotes */
static inline void jbuf_sv(JsonBuf *b, Sv s)
{
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

/* Append a raw string slice (start..end pointers, no escaping) as a
 * JSON string — used when emitting sj_Value.start..end back into output */
static inline void jbuf_sj_str(JsonBuf *b, const char *start, const char *end)
{
    jbuf_raw(b, "\"", 1);
    for (const char *p = start; p < end; p++) {
        if ('"'  == *p) { jbuf_raw(b, "\\\"", 2); continue; }
        if ('\\' == *p) { jbuf_raw(b, "\\\\", 2); continue; }
        jbuf_raw(b, p, 1);
    }
    jbuf_raw(b, "\"", 1);
}

static inline void jbuf_int(JsonBuf *b, long long v)
{
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%lld", v);
    jbuf_cstr(b, tmp);
}

static inline void jbuf_bool(JsonBuf *b, bool v)
{
    jbuf_cstr(b, v ? "true" : "false");
}

static inline void jbuf_null(JsonBuf *b) { jbuf_cstr(b, "null"); }

/* Append "key": — caller appends value immediately after */
static inline void jbuf_key(JsonBuf *b, const char *key)
{
    jbuf_str(b, key);
    jbuf_raw(b, ":", 1);
}

static inline Sv jbuf_sv_result(const JsonBuf *b)
{
    return (Sv){ b->buf, b->len };
}

#endif /* JSON_H */
