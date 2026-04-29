/*
 * odoo.c — Odoo XML-RPC implementation
 *
 * Builds minimal XML-RPC envelopes as strings (no DOM, no deps).
 * Parses responses with a two-pass approach:
 *   1. Check for <fault>
 *   2. Extract the <value> content and return it as-is for MCP tools/call
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * MIT License
 */
#include "odoo.h"
#include "net.h"
#include "arena.h"
#include "sv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Forward declarations ───────────────────────────────────────────────── */
const char *json_to_xmlrpc(Arena *a, const char *json_str);
const char *jval_to_xmlrpc(Arena *a, const JsonVal *v);

/* ── XML-RPC envelope builders ─────────────────────────────────────────── */

/*
 * xmlrpc_val — wrap a raw JSON-like value as XML-RPC <value>.
 * We pass already-JSON-serialised args from the MCP layer, so we need
 * to convert JSON → XML-RPC types at this boundary.
 *
 * For our four tools the args are always:
 *   authenticate: string, string, string, struct{}
 *   execute_kw:   string, int, string, string, string, array, struct
 *
 * We keep it simple: strings → <string>, ints → <int>, {} → <struct/>.
 */
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

/*
 * Build a complete XML-RPC methodCall envelope.
 * params_xml — pre-rendered <param>...</param> block(s)
 */
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

/* Wrap a single value in <param> tags */
static const char *xmlrpc_param(Arena *a, const char *val_xml)
{
    return arena_sprintf(a, "<param>%s</param>", val_xml);
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

/* ── Extract raw value content from XML-RPC response ───────────────────── */

/*
 * xmlrpc_extract_value — find <value>...</value> in response params.
 * Returns a pointer into resp (NOT null-terminated at the right end).
 * Writes length to *out_len. Returns NULL on parse failure.
 *
 * For our use case we return the raw content between the outermost
 * <value>...</value> so MCP layer can forward it as text/JSON.
 */
static const char *xmlrpc_extract_value(const char *resp, size_t *out_len)
{
    const char *start = strstr(resp, "<params><param><value>");
    if (NULL == start) {
        start = strstr(resp, "<value>");
        if (NULL == start) return NULL;
        start += 7;
    } else {
        start += strlen("<params><param><value>");
    }

    const char *end = strstr(start, "</value>");
    if (NULL == end) return NULL;
    *out_len = (size_t)(end - start);
    return start;
}

/*
 * xmlrpc_int_value — extract an integer from <value><int>N</int></value>
 */
static int xmlrpc_int_value(const char *resp)
{
    const char *tags[] = { "<int>", "<i4>", NULL };
    for (int i = 0; tags[i]; i++) {
        const char *p = strstr(resp, tags[i]);
        if (p) return atoi(p + strlen(tags[i]));
    }
    return 0;
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
        fprintf(stderr, "odoo: auth: bad credentials for user %s\n",
                ctx->cfg->odoo_user);
        return -1;
    }

    int uid = xmlrpc_int_value(resp);
    if (uid <= 0) {
        fprintf(stderr, "odoo: auth: invalid uid %d returned\n", uid);
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

    /*
     * execute_kw positional args:
     *   db (string), uid (int), api_key (string),
     *   model (string), method (string),
     *   args (array — passed through as raw XML-RPC from args_json),
     *   kwargs (struct — passed through from kwargs_json)
     *
     * For simplicity we pass args_json and kwargs_json as <string> values
     * and rely on the Odoo side to interpret them.  The real approach is
     * to convert JSON arrays/objects to XML-RPC arrays/structs.
     * We implement the full converter below.
     */
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

    /* Copy response body into caller's out buffer */
    size_t rlen = (size_t)n < olen - 1 ? (size_t)n : olen - 1;
    memcpy(out, resp, rlen);
    out[rlen] = '\0';
    return (int)rlen;
}

/* ── JSON → XML-RPC converter ───────────────────────────────────────────── */

/*
 * json_to_xmlrpc — convert a JSON string to an XML-RPC <value> element.
 * Handles string, number, bool, null, array, object recursively.
 * Uses the arena for all allocations.
 */
const char *json_to_xmlrpc(Arena *a, const char *json_str)
{
    if (NULL == json_str || '\0' == *json_str)
        return xmlrpc_empty_struct(a);

    char errbuf[128] = {0};
    JsonVal *v = json_parse(json_str, strlen(json_str), a, errbuf, sizeof errbuf);
    if (NULL == v) {
        fprintf(stderr, "odoo: json_to_xmlrpc parse error: %s\n", errbuf);
        return xmlrpc_string(a, json_str); /* fallback: pass as string */
    }
    return jval_to_xmlrpc(a, v);
}

const char *jval_to_xmlrpc(Arena *a, const JsonVal *v)
{
    if (NULL == v) return xmlrpc_empty_struct(a);

    switch (v->type) {
    case JSON_NULL:
        return arena_strdup(a, "<value><nil/></value>");

    case JSON_BOOL:
        return arena_sprintf(a,
            "<value><boolean>%d</boolean></value>", v->u.b ? 1 : 0);

    case JSON_NUMBER: {
        long long iv = (long long)v->u.n;
        if ((double)iv == v->u.n)
            return arena_sprintf(a, "<value><int>%lld</int></value>", iv);
        return arena_sprintf(a, "<value><double>%g</double></value>", v->u.n);
    }

    case JSON_STRING: {
        /* null-terminate the sv */
        char *s = (char *)arena_alloc(a, v->u.s.len + 1);
        memcpy(s, v->u.s.data, v->u.s.len);
        s[v->u.s.len] = '\0';
        return xmlrpc_string(a, s);
    }

    case JSON_ARRAY: {
        /* <value><array><data>items...</data></array></value> */
        char *items = arena_strdup(a, "");
        for (const JsonVal *item = v->u.items; item; item = item->next) {
            const char *xv = jval_to_xmlrpc(a, item);
            items = arena_sprintf(a, "%s%s", items, xv);
        }
        return arena_sprintf(a,
            "<value><array><data>%s</data></array></value>", items);
    }

    case JSON_OBJECT: {
        /* <value><struct>members...</struct></value> */
        char *members = arena_strdup(a, "");
        for (const JsonPair *pr = v->u.pairs; pr; pr = pr->next) {
            char *key = (char *)arena_alloc(a, pr->key.len + 1);
            memcpy(key, pr->key.data, pr->key.len);
            key[pr->key.len] = '\0';
            const char *xv = jval_to_xmlrpc(a, pr->val);
            members = arena_sprintf(a,
                "%s<member><name>%s</name>%s</member>",
                members, key, xv);
        }
        return arena_sprintf(a,
            "<value><struct>%s</struct></value>", members);
    }

    default:
        return xmlrpc_empty_struct(a);
    }
}
