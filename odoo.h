/*
 * odoo.h — Odoo 15+ XML-RPC client
 *
 * Hand-rolled XML-RPC over net_http_post().
 * No libxml, no expat — just string building + minimal SAX pull.
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#ifndef ODOO_H
#define ODOO_H

#include <stddef.h>
#include <stdbool.h>
#include "arena.h"
#include "json.h"
#include "config.h"

#define ODOO_RESP_MAX (256 * 1024)  /* 256 KiB max XML-RPC response */

typedef struct {
    const Config *cfg;
    int           uid;      /* cached auth uid, 0 = not authenticated */
} OdooCtx;

/*
 * odoo_auth — authenticate, cache uid.
 * Returns uid > 0 on success, -1 on failure.
 */
int odoo_auth(OdooCtx *ctx, Arena *a);

/*
 * odoo_execute — call execute_kw on any model.
 *
 * model   — e.g. "res.partner"
 * method  — e.g. "search_read"
 * args    — JSON array string of positional args (already serialised)
 * kwargs  — JSON object string of keyword args  (already serialised)
 * out     — caller buffer to receive raw XML-RPC response body
 * olen    — size of out
 *
 * Returns response byte count or -1 on error.
 */
int odoo_execute(OdooCtx *ctx,
                 const char *model,  const char *method,
                 const char *args,   const char *kwargs,
                 char *out, size_t olen,
                 Arena *a);

#endif /* ODOO_H */
