/*
 * main.c — kcgi HTTP server for odoo-mcp-server (native target)
 *
 * Routes:
 *   POST /mcp      — MCP JSON-RPC 2.0 (Streamable HTTP, stateless)
 *   GET  /healthz  — liveness probe
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#ifndef __wasm__   /* native only — WASM target uses worker.js instead */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <kcgi.h>
#include <kcgijson.h>

#include "config.h"
#include "arena.h"
#include "mcp.h"
#include "odoo.h"

#define REQ_MAX    (256 * 1024)   /* 256 KiB max inbound MCP request */
#define RESP_MAX   (512 * 1024)   /* 512 KiB max outbound response    */
#define ARENA_SIZE (4 * 1024 * 1024) /* 4 MiB per-request arena       */

/* ── kcgi page/key tables ────────────────────────────────────────────────── */

enum Page { PAGE_MCP, PAGE_HEALTHZ, PAGE__MAX };

static const char *PAGES[PAGE__MAX] = { "mcp", "healthz" };

/* No query keys needed — we read raw body */
static const struct kvalid KEYS[] = {{ NULL, 0 }};

/* ── /healthz ────────────────────────────────────────────────────────────── */

static void handle_healthz(struct kreq *r, OdooCtx *ctx, Arena *a)
{
    int uid = odoo_auth(ctx, a);

    khttp_head(r, kresps[KRESP_STATUS],
               uid > 0 ? "%s" : "%s",
               uid > 0 ? khttps[KHTTP_200] : khttps[KHTTP_503]);
    khttp_head(r, kresps[KRESP_CONTENT_TYPE], "%s", kmimetypes[KMIME_APP_JSON]);
    khttp_body(r);

    struct kjsonreq jreq;
    kjson_open(&jreq, r);
    kjson_obj_open(&jreq);
    kjson_putstringp(&jreq, "status",   uid > 0 ? "ok" : "error");
    kjson_putintp   (&jreq, "odoo_uid", uid);
    kjson_obj_close(&jreq);
    kjson_close(&jreq);
}

/* ── /mcp ────────────────────────────────────────────────────────────────── */

static void handle_mcp(struct kreq *r, OdooCtx *ctx, Arena *a)
{
    /* Only POST allowed */
    if (KMETHOD_POST != r->method) {
        khttp_head(r, kresps[KRESP_STATUS],
                   "%s", khttps[KHTTP_405]);
        khttp_head(r, kresps[KRESP_CONTENT_TYPE],
                   "%s", kmimetypes[KMIME_APP_JSON]);
        khttp_body(r);
        khttp_puts(r, "{\"error\":\"Method Not Allowed\"}");
        return;
    }

    /* Read raw POST body */
    char  *body = (char *)arena_alloc(a, REQ_MAX);
    size_t blen = 0;

    /* kcgi stores the raw body in r->fieldmap when mime is unknown,
     * but for JSON we read via r->rawbuflen / r->rawbuf.
     * Use the raw body directly. */
    if (r->rawbuflen && r->rawbuflen < REQ_MAX) {
        memcpy(body, r->rawbuf, r->rawbuflen);
        blen = r->rawbuflen;
    } else if (r->fieldmap) {
        /* fallback: try first field value as raw body */
        const char *fv = r->fieldmap[0].val;
        if (fv) { blen = strlen(fv); memcpy(body, fv, blen); }
    }

    if (0 == blen) {
        khttp_head(r, kresps[KRESP_STATUS],   "%s", khttps[KHTTP_400]);
        khttp_head(r, kresps[KRESP_CONTENT_TYPE], "%s", kmimetypes[KMIME_APP_JSON]);
        khttp_body(r);
        khttp_puts(r, "{\"error\":\"empty body\"}");
        return;
    }

    char *resp = (char *)arena_alloc(a, RESP_MAX);
    int   n    = mcp_handle(body, blen, resp, RESP_MAX, ctx, a);

    /* n == 0 means notification (no response per spec) */
    if (0 == n) {
        khttp_head(r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_204]);
        khttp_body(r);
        return;
    }

    khttp_head(r, kresps[KRESP_STATUS],       "%s", khttps[KHTTP_200]);
    khttp_head(r, kresps[KRESP_CONTENT_TYPE], "%s", kmimetypes[KMIME_APP_JSON]);
    /* MCP session header — stateless mode: echo a fixed session id */
    khttp_head(r, kresps[KRESP_CACHE_CONTROL], "%s", "no-store");
    khttp_body(r);
    khttp_write(r, resp, (size_t)n);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    Config  cfg = config_load();
    OdooCtx ctx = { &cfg, 0 };
    Arena   a   = arena_new(ARENA_SIZE);

    struct kreq r;
    struct kfcgi *fcgi = NULL;

    /* Try FastCGI first, fall back to plain CGI */
    if (KCGI_OK != khttp_fcgi_init(&fcgi, KEYS, 0, PAGES, PAGE__MAX, PAGE_MCP)) {
        fcgi = NULL;
    }

    for (;;) {
        enum kcgi_err ke;

        if (fcgi)
            ke = khttp_fcgi_parse(fcgi, &r);
        else
            ke = khttp_parse(&r, KEYS, 0, PAGES, PAGE__MAX, PAGE_MCP);

        if (KCGI_OK != ke) break;

        arena_reset(&a);   /* recycle arena each request */

        switch (r.page) {
        case PAGE_MCP:     handle_mcp    (&r, &ctx, &a); break;
        case PAGE_HEALTHZ: handle_healthz(&r, &ctx, &a); break;
        default:
            khttp_head(&r, kresps[KRESP_STATUS], "%s", khttps[KHTTP_404]);
            khttp_body(&r);
            khttp_puts(&r, "not found");
        }

        khttp_free(&r);
        if (!fcgi) break;   /* CGI: one request per invocation */
    }

    if (fcgi) khttp_fcgi_free(fcgi);
    arena_free(&a);
    return 0;
}

#endif /* !__wasm__ */
