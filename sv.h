/*
 * sv.h — string view (tsoding/sv style)
 * C99, zero deps, zero alloc
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * Public Domain
 */
#ifndef SV_H
#define SV_H

#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { const char *data; size_t len; } Sv;

#define SV(cstr)        ((Sv){ (cstr), strlen(cstr) })
#define SV_LIT(lit)     ((Sv){ (lit),  sizeof(lit) - 1 })
#define SV_FMT          "%.*s"
#define SV_ARG(sv)      (int)(sv).len, (sv).data
#define SV_NULL         ((Sv){ NULL, 0 })

static inline Sv   sv_from_cstr(const char *s) { return (Sv){ s, s ? strlen(s) : 0 }; }
static inline bool sv_eq(Sv a, Sv b)  { return a.len == b.len && 0 == memcmp(a.data, b.data, a.len); }
static inline bool sv_empty(Sv s)     { return 0 == s.len || NULL == s.data; }

static inline Sv sv_chop_by_delim(Sv *s, char delim)
{
    size_t i = 0;
    while (i < s->len && s->data[i] != delim) i++;
    Sv tok = { s->data, i };
    if (i < s->len) { s->data += i + 1; s->len -= i + 1; }
    else            { s->data += i;     s->len  = 0; }
    return tok;
}

static inline Sv sv_trim_left(Sv s)
{
    while (s.len && (s.data[0] == ' ' || s.data[0] == '\t' ||
                     s.data[0] == '\n' || s.data[0] == '\r'))
    { s.data++; s.len--; }
    return s;
}

static inline Sv sv_trim_right(Sv s)
{
    while (s.len && (s.data[s.len-1] == ' ' || s.data[s.len-1] == '\t' ||
                     s.data[s.len-1] == '\n' || s.data[s.len-1] == '\r'))
        s.len--;
    return s;
}

static inline Sv sv_trim(Sv s) { return sv_trim_left(sv_trim_right(s)); }

static inline bool sv_starts_with(Sv s, Sv prefix)
{
    return s.len >= prefix.len && 0 == memcmp(s.data, prefix.data, prefix.len);
}

#endif /* SV_H */
