/*
 * net.c — BSD sockets + libtls HTTPS POST (native target only)
 *
 * Excluded from WASM build — net.h macro handles that target.
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#ifndef __wasm__

#include "net.h"
#include "sv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

/* libtls: FreeBSD ships LibreSSL; Linux needs libretls (pkg: libretls-dev)
 * or LibreSSL installed manually. Both provide <tls.h>. */
#if defined(__linux__)
#  include <tls.h>
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  include <tls.h>
#else
#  include <tls.h>
#endif

#define NET_READ_BUF  (64 * 1024)  /* 64 KiB read buffer */

/* ── URL parsing ────────────────────────────────────────────────────────── */

typedef struct {
    char host[256];
    char port[8];
    char path[1024];
} ParsedURL;

static int parse_url(const char *url, ParsedURL *out)
{
    /* Expect https://host[:port]/path */
    const char *p = url;
    if (0 != strncmp(p, "https://", 8)) {
        fprintf(stderr, "net: only https:// URLs supported\n");
        return -1;
    }
    p += 8;

    /* host */
    const char *host_end = strpbrk(p, ":/");
    if (NULL == host_end) host_end = p + strlen(p);
    size_t hlen = (size_t)(host_end - p);
    if (hlen >= sizeof out->host) return -1;
    memcpy(out->host, p, hlen);
    out->host[hlen] = '\0';
    p = host_end;

    /* optional port */
    if (':' == *p) {
        p++;
        const char *port_end = strchr(p, '/');
        if (NULL == port_end) port_end = p + strlen(p);
        size_t plen = (size_t)(port_end - p);
        if (plen >= sizeof out->port) return -1;
        memcpy(out->port, p, plen);
        out->port[plen] = '\0';
        p = port_end;
    } else {
        strncpy(out->port, "443", sizeof out->port - 1);
    }

    /* path */
    if ('\0' == *p || '/' != *p)
        strncpy(out->path, "/", sizeof out->path - 1);
    else
        strncpy(out->path, p, sizeof out->path - 1);

    return 0;
}

/* ── TCP connect ────────────────────────────────────────────────────────── */

static int tcp_connect(const char *host, const char *port)
{
    struct addrinfo hints = {0};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (0 != rc) {
        fprintf(stderr, "net: getaddrinfo %s:%s: %s\n",
                host, port, gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *r = res; r; r = r->ai_next) {
        fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (-1 == fd) continue;
        if (0 == connect(fd, r->ai_addr, r->ai_addrlen)) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (-1 == fd)
        fprintf(stderr, "net: connect %s:%s failed\n", host, port);
    return fd;
}

/* ── net_http_post ──────────────────────────────────────────────────────── */

int net_http_post(const char *url,
                  const char *body, size_t blen,
                  char *out, size_t olen,
                  Arena *a)
{
    (void)a; /* scratch arena — available for future use */

    ParsedURL pu = {0};
    if (0 != parse_url(url, &pu)) return -1;

    int fd = tcp_connect(pu.host, pu.port);
    if (-1 == fd) return -1;

    /* TLS setup */
    struct tls_config *cfg = tls_config_new();
    if (NULL == cfg) { close(fd); return -1; }

    struct tls *ctx = tls_client();
    if (NULL == ctx) { tls_config_free(cfg); close(fd); return -1; }

    if (0 != tls_configure(ctx, cfg)) {
        fprintf(stderr, "net: tls_configure: %s\n", tls_error(ctx));
        goto fail;
    }

    if (0 != tls_connect_socket(ctx, fd, pu.host)) {
        fprintf(stderr, "net: tls_connect: %s\n", tls_error(ctx));
        goto fail;
    }

    /* Build HTTP/1.1 request */
    char req[4096];
    int  rlen = snprintf(req, sizeof req,
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: text/xml; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        pu.path, pu.host, blen);

    if (rlen < 0 || (size_t)rlen >= sizeof req) goto fail;

    /* Send headers + body */
    if (tls_write(ctx, req, (size_t)rlen) < 0) {
        fprintf(stderr, "net: tls_write headers: %s\n", tls_error(ctx));
        goto fail;
    }
    if (blen && tls_write(ctx, body, blen) < 0) {
        fprintf(stderr, "net: tls_write body: %s\n", tls_error(ctx));
        goto fail;
    }

    /* Read full response into a temp buffer */
    char   *rbuf = (char *)malloc(NET_READ_BUF);
    if (NULL == rbuf) goto fail;
    size_t  roff = 0;
    ssize_t n;

    while ((n = tls_read(ctx, rbuf + roff, NET_READ_BUF - roff - 1)) > 0) {
        roff += (size_t)n;
        if (roff >= NET_READ_BUF - 1) break;
    }
    rbuf[roff] = '\0';

    /* Strip HTTP headers — find \r\n\r\n */
    char *body_start = strstr(rbuf, "\r\n\r\n");
    if (NULL == body_start) {
        free(rbuf);
        goto fail;
    }
    body_start += 4;
    size_t resp_len = (size_t)(rbuf + roff - body_start);
    if (resp_len >= olen) resp_len = olen - 1;
    memcpy(out, body_start, resp_len);
    out[resp_len] = '\0';

    free(rbuf);
    tls_close(ctx);
    tls_free(ctx);
    tls_config_free(cfg);
    close(fd);
    return (int)resp_len;

fail:
    tls_close(ctx);
    tls_free(ctx);
    tls_config_free(cfg);
    close(fd);
    return -1;
}

#endif /* !__wasm__ */
