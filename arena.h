/*
 * arena.h — bump-pointer arena allocator
 * tsoding style, C99, zero deps
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * Public Domain
 */
#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ARENA_DEFAULT_CAP (8 * 1024 * 1024)  /* 8 MiB */
#define ARENA_ALIGN       (sizeof(void *))

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
} Arena;

static inline Arena arena_new(size_t cap)
{
    Arena a = {0};
    a.buf = (uint8_t *)malloc(cap);
    assert(a.buf && "arena_new: malloc failed");
    a.cap = cap;
    a.pos = 0;
    return a;
}

static inline void *arena_alloc(Arena *a, size_t size)
{
    size_t aligned = (size + ARENA_ALIGN - 1) & ~(ARENA_ALIGN - 1);
    assert(a->pos + aligned <= a->cap && "arena_alloc: out of memory");
    void *ptr = a->buf + a->pos;
    a->pos += aligned;
    memset(ptr, 0, size);
    return ptr;
}

static inline char *arena_strdup(Arena *a, const char *s)
{
    if (NULL == s) return NULL;
    size_t n   = strlen(s) + 1;
    char  *dst = (char *)arena_alloc(a, n);
    memcpy(dst, s, n);
    return dst;
}

/* printf into arena — returns pointer to null-terminated string */
#include <stdio.h>
#include <stdarg.h>
static inline char *arena_sprintf(Arena *a, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    assert(n >= 0);
    char *buf = (char *)arena_alloc(a, (size_t)(n + 1));
    va_start(ap, fmt);
    vsnprintf(buf, (size_t)(n + 1), fmt, ap);
    va_end(ap);
    return buf;
}

static inline void arena_reset(Arena *a) { a->pos = 0; }

static inline void arena_free(Arena *a)
{
    free(a->buf);
    a->buf = NULL;
    a->cap = a->pos = 0;
}

#endif /* ARENA_H */
