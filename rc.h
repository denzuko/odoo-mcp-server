#ifndef RC_H_
#define RC_H_

#include <stddef.h>

typedef struct {
    ptrdiff_t count;
    void (*destroy)(void *data);
} Rc;

void *rc_alloc(size_t size, void (*destroy)(void *data));
void *rc_acquire(void *data);
void rc_release(void *data);
ptrdiff_t rc_count(void *data);

#endif // RC_H_

#ifdef RC_IMPLEMENTATION

void *rc_alloc(size_t size, void (*destroy)(void *data))
{
    Rc *rc = malloc(sizeof(Rc) + size);
    assert(rc);
    rc->count = 0;
    rc->destroy = destroy;
    printf("[RC] %p allocated\n", (void *)rc);
    return rc + 1;
}

void *rc_acquire(void *data)
{
    Rc *rc = (Rc*)data - 1;
    rc->count += 1;
    // printf("[RC] %p acquired\n", rc);
    return data;
}

void rc_release(void *data)
{
    Rc *rc = (Rc*)data - 1;
    rc->count -= 1;
    if (rc->count <= 0) {
        rc->destroy(rc + 1);
        printf("[RC] %p released\n", (void *)rc);
        free(rc);
    }
}

ptrdiff_t rc_count(void *data)
{
    Rc *rc = (Rc*)data - 1;
    return rc->count;
}

#endif // RC_IMPLEMENTATION
