/*
 * net.h — target-agnostic HTTP POST transport
 *
 * Native:  BSD sockets + libtls  (implemented in net.c)
 * WASM:    imports http_fetch from the CF Worker JS shim
 *
 * Callers (odoo.c) only see net_http_post() — never a socket.
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * MIT License
 */
#ifndef NET_H
#define NET_H

#include <stddef.h>
#include "arena.h"

#ifdef __wasm__

/*
 * Imported from the Cloudflare Worker JS shim (worker.js).
 * The shim bridges this call to the Workers fetch() API.
 *
 * url/url_len — full HTTPS URL (not null-terminated in the call)
 * body/body_len — raw POST body bytes
 * out/out_max — response body written here, null-terminated
 * Returns number of bytes written (< out_max), or -1 on error.
 */
__attribute__((import_module("env"), import_name("http_fetch")))
extern int wasm_http_fetch(
    const char *url,  size_t url_len,
    const char *body, size_t body_len,
    char       *out,  size_t out_max);

#define net_http_post(url, body, blen, out, olen, _arena) \
    wasm_http_fetch((url), strlen(url), (body), (blen), (out), (olen))

#else /* native */

/*
 * net_http_post — synchronous HTTPS POST via BSD sockets + libtls.
 *
 * url      — full URL including scheme, e.g. "https://dapla.net/xmlrpc/2/common"
 * body     — request body (XML-RPC envelope)
 * blen     — body length in bytes
 * out      — caller-supplied buffer for response body
 * olen     — size of out buffer
 * a        — arena for internal scratch allocations
 *
 * Returns number of bytes written to out (response body only, stripped of
 * HTTP headers), or -1 on error. out is always null-terminated on success.
 */
int net_http_post(const char *url,
                  const char *body, size_t blen,
                  char *out, size_t olen,
                  Arena *a);

#endif /* __wasm__ */

#endif /* NET_H */
