/*
 * odoo.c — Odoo XML-RPC client
 *
 * Builds XML-RPC envelopes as plain strings (no DOM).
 * Converts JSON arguments to XML-RPC using rxi/sj.h (zero-alloc cursor).
 * All allocations go through the per-request Arena.
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#include "odoo.h"
#include "net.h"
#include "arena.h"
#include "sj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── XML-RPC envelope builders ─────────────────────────────────────────── */

static const char *xmlrpc_string(Arena *a, const char *s)
{
    return arena_sprintf(a, "<value><string>%s</string></value>", s);
}

static const char *xmlrpc_int(Arena *a, int n)
{
    return arena_sprintf(a, "<value><int>%d</int></value>", n);
}

static const char *xmlrpc_empty_struct(Arena *a)
{
    return arena_strdup(a, "<value><struct></struct></value>");
}

static const char *xmlrpc_param(Arena *a, const char *val_xml)
{
    return arena_sprintf(a, "<param>%s</param>", val_xml);
}

static const char *xmlrpc_envelope(Arena *a,
                                   const char *method,
                                   const char *params_xml)
{
    return arena_sprintf(a,
        "<?xml version=\"1.0\"?>"
        "<methodCall>"
          "<methodName>%s</methodName>"
          "<params>%s</params>"
        "</methodCall>",
        method, params_xml);
}

/* ── Fault detection ────────────────────────────────────────────────────── */

static bool xmlrpc_is_fault(const char *resp)
{
    return NULL != strstr(resp, "<fault>");
}

static void xmlrpc_log_fault(const char *resp)
{
    fprintf(stderr, "odoo: XML-RPC fault: %.512s\n", resp);
}

/* ── Integer extraction from XML-RPC response ───────────────────────────── */

static int xmlrpc_int_value(const char *resp)
{
    const char *tags[] = { "<int>", "<i4>", NULL };
    for (int i = 0; NULL != tags[i]; i++) {
        const char *p = strstr(resp, tags[i]);
        if (NULL != p) return atoi(p + strlen(tags[i]));
    }
    return 0;
}

/* ── JSON → XML-RPC converter using sj.h ───────────────────────────────── */
/*
 * sj.h is a non-allocating cursor — we walk the JSON structure and
 * emit XML-RPC incrementally into arena-backed strings.
 */

/* Forward declaration */
static const char *sj_to_xmlrpc(Arena *a, sj_Reader *r, sj_Value v,
                                 const char *json_end);

static const char *sj_to_xmlrpc(Arena *a, sj_Reader *r, sj_Value v,
                                 const char *json_end)
{
    switch (v.type) {
    case SJ_NULL:
        return arena_strdup(a, "<value><nil/></value>");

    case SJ_BOOL: {
        bool is_true = ((v.end - v.start) == 4); /* "true" is 4 chars */
        return arena_sprintf(a,
            "<value><boolean>%d</boolean></value>", is_true ? 1 : 0);
    }

    case SJ_NUMBER: {
        char tmp[64] = {0};
        size_t n = (size_t)(v.end - v.start);
        if (n >= sizeof tmp) n = sizeof(tmp) - 1;
        memcpy(tmp, v.start, n);
        /* Integer if no decimal point or exponent */
        if (NULL == strpbrk(tmp, ".eE")) {
            long long iv = strtoll(tmp, NULL, 10);
            return arena_sprintf(a,
                "<value><int>%lld</int></value>", iv);
        }
        double dv = strtod(tmp, NULL);
        return arena_sprintf(a,
            "<value><double>%g</double></value>", dv);
    }

    case SJ_STRING: {
        /* v.start/end are inside the quotes — null-terminate via arena */
        size_t n = (size_t)(v.end - v.start);
        char  *s = (char *)arena_alloc(a, n + 1);
        memcpy(s, v.start, n);
        s[n] = '\0';
        return xmlrpc_string(a, s);
    }

    case SJ_ARRAY: {
        /* Iterate children and build <array><data>...</data></array> */
        char *items = arena_strdup(a, "");
        sj_Value item;
        while (sj_iter_array(r, v, &item)) {
            const char *xv = sj_to_xmlrpc(a, r, item, json_end);
            items = arena_sprintf(a, "%s%s", items, xv);
        }
        return arena_sprintf(a,
            "<value><array><data>%s</data></array></value>", items);
    }

    case SJ_OBJECT: {
        char *members = arena_strdup(a, "");
        sj_Value key, val;
        while (sj_iter_object(r, v, &key, &val)) {
            /* key is a SJ_STRING — copy without quotes */
            size_t kn = (size_t)(key.end - key.start);
            char  *ks = (char *)arena_alloc(a, kn + 1);
            memcpy(ks, key.start, kn);
            ks[kn] = '\0';
            const char *xv = sj_to_xmlrpc(a, r, val, json_end);
            members = arena_sprintf(a,
                "%s<member><name>%s</name>%s</member>",
                members, ks, xv);
        }
        return arena_sprintf(a,
            "<value><struct>%s</struct></value>", members);
    }

    default:
        return xmlrpc_empty_struct(a);
    }
}

/*
 * json_to_xmlrpc — entry point.
 * Takes a null-terminated JSON string and returns XML-RPC <value> element.
 */
const char *json_to_xmlrpc(Arena *a, const char *json_str)
{
    if (NULL == json_str || '\0' == *json_str)
        return xmlrpc_empty_struct(a);

    /* sj_reader requires mutable char* */
    size_t len = strlen(json_str);
    char  *buf = (char *)arena_alloc(a, len + 1);
    memcpy(buf, json_str, len);
    buf[len] = '\0';

    sj_Reader r   = sj_reader(buf, len);
    sj_Value  v   = sj_read(&r);

    if (SJ_ERROR == v.type) {
        fprintf(stderr, "odoo: json_to_xmlrpc: %s\n",
                r.error ? r.error : "unknown error");
        return xmlrpc_string(a, json_str); /* fallback */
    }

    return sj_to_xmlrpc(a, &r, v, buf + len);
}

/* ── odoo_auth ──────────────────────────────────────────────────────────── */

int odoo_auth(OdooCtx *ctx, Arena *a)
{
    if (ctx->uid > 0) return ctx->uid;

    const char *params = arena_sprintf(a, "%s%s%s%s",
        xmlrpc_param(a, xmlrpc_string(a, ctx->cfg->odoo_db)),
        xmlrpc_param(a, xmlrpc_string(a, ctx->cfg->odoo_user)),
        xmlrpc_param(a, xmlrpc_string(a, ctx->cfg->odoo_apikey)),
        xmlrpc_param(a, xmlrpc_empty_struct(a)));

    const char *envelope = xmlrpc_envelope(a, "authenticate", params);
    const char *url      = arena_sprintf(a, "%s/xmlrpc/2/common",
                                         ctx->cfg->odoo_url);

    char *resp = (char *)arena_alloc(a, ODOO_RESP_MAX);
    int   n    = net_http_post(url, envelope, strlen(envelope),
                               resp, ODOO_RESP_MAX, a);
    if (n < 0) {
        fprintf(stderr, "odoo: auth: network error\n");
        return -1;
    }
    if (xmlrpc_is_fault(resp)) {
        xmlrpc_log_fault(resp);
        return -1;
    }

    int uid = xmlrpc_int_value(resp);
    if (uid <= 0) {
        fprintf(stderr, "odoo: auth: invalid uid %d\n", uid);
        return -1;
    }
    ctx->uid = uid;
    return uid;
}

/* ── odoo_execute ───────────────────────────────────────────────────────── */

int odoo_execute(OdooCtx *ctx,
                 const char *model,  const char *method,
                 const char *args_json, const char *kwargs_json,
                 char *out, size_t olen,
                 Arena *a)
{
    int uid = odoo_auth(ctx, a);
    if (uid < 0) return -1;

    const char *xml_args   = json_to_xmlrpc(a, args_json);
    const char *xml_kwargs = json_to_xmlrpc(a, kwargs_json);

    const char *params = arena_sprintf(a, "%s%s%s%s%s%s%s",
        xmlrpc_param(a, xmlrpc_string(a, ctx->cfg->odoo_db)),
        xmlrpc_param(a, xmlrpc_int(a, uid)),
        xmlrpc_param(a, xmlrpc_string(a, ctx->cfg->odoo_apikey)),
        xmlrpc_param(a, xmlrpc_string(a, model)),
        xmlrpc_param(a, xmlrpc_string(a, method)),
        xmlrpc_param(a, xml_args),
        xmlrpc_param(a, xml_kwargs));

    const char *envelope = xmlrpc_envelope(a, "execute_kw", params);
    const char *url      = arena_sprintf(a, "%s/xmlrpc/2/object",
                                         ctx->cfg->odoo_url);

    char *resp = (char *)arena_alloc(a, ODOO_RESP_MAX);
    int   n    = net_http_post(url, envelope, strlen(envelope),
                               resp, ODOO_RESP_MAX, a);
    if (n < 0) return -1;
    if (xmlrpc_is_fault(resp)) {
        xmlrpc_log_fault(resp);
        return -1;
    }

    size_t rlen = (size_t)n < olen - 1 ? (size_t)n : olen - 1;
    memcpy(out, resp, rlen);
    out[rlen] = '\0';
    return (int)rlen;
}
