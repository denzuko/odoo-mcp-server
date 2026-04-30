/*
 * mcp.c — MCP JSON-RPC 2.0 dispatcher
 *
 * Parses inbound requests with rxi/sj.h (public domain, zero-alloc cursor).
 * Builds responses with JsonBuf (arena-backed builder from json.h).
 * SJ_IMPL is defined here — exactly one translation unit.
 *
 * Wire format (MCP 2025-03-26):
 *   POST /mcp  Content-Type: application/json
 *   {"jsonrpc":"2.0","id":N,"method":"...","params":{...}}
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */

#define SJ_IMPL
#include "sj.h"

#include "mcp.h"
#include "json.h"
#include "odoo.h"
#include "arena.h"
#include "sv.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Helpers: sj_Value comparisons ─────────────────────────────────────── */

/* Compare a sj_Value (pointer range) to a C string — no alloc */
static bool sj_eq(sj_Value v, const char *s)
{
    size_t n = (size_t)(v.end - v.start);
    return strlen(s) == n && 0 == memcmp(v.start, s, n);
}

/* Copy a sj_Value string into the arena as a null-terminated C string */
static const char *sj_dup(Arena *a, sj_Value v)
{
    size_t n = (size_t)(v.end - v.start);
    char  *p = (char *)arena_alloc(a, n + 1);
    memcpy(p, v.start, n);
    p[n] = '\0';
    return p;
}

/* strtoll over a sj_Value range */
static long long sj_ll(sj_Value v)
{
    char tmp[32] = {0};
    size_t n = (size_t)(v.end - v.start);
    if (n >= sizeof tmp) n = sizeof(tmp) - 1;
    memcpy(tmp, v.start, n);
    return strtoll(tmp, NULL, 10);
}

/* strtod over a sj_Value range */
static double sj_dbl(sj_Value v)
{
    char tmp[64] = {0};
    size_t n = (size_t)(v.end - v.start);
    if (n >= sizeof tmp) n = sizeof(tmp) - 1;
    memcpy(tmp, v.start, n);
    return strtod(tmp, NULL);
}

/* ── JSON-RPC response builders ─────────────────────────────────────────── */

/*
 * Emit the id field from the sj_Reader position at the point where
 * "id" value was read.  We store the raw sj_Value for id so we can
 * re-emit it verbatim (number, string, or null).
 */
static void jbuf_id(JsonBuf *b, sj_Value id)
{
    if (SJ_NULL == id.type || SJ_ERROR == id.type || SJ_END == id.type) {
        jbuf_null(b);
    } else if (SJ_NUMBER == id.type) {
        jbuf_raw(b, id.start, (size_t)(id.end - id.start));
    } else if (SJ_STRING == id.type) {
        jbuf_sj_str(b, id.start, id.end);
    } else {
        jbuf_null(b);
    }
}

static int jrpc_ok(JsonBuf *b, sj_Value id, const char *result_json)
{
    jbuf_cstr(b, "{");
    jbuf_key(b, "jsonrpc"); jbuf_str(b, "2.0"); jbuf_cstr(b, ",");
    jbuf_key(b, "id");      jbuf_id(b, id);     jbuf_cstr(b, ",");
    jbuf_key(b, "result");  jbuf_cstr(b, result_json);
    jbuf_cstr(b, "}");
    return (int)b->len;
}

static int jrpc_err(JsonBuf *b, sj_Value id, int code, const char *msg)
{
    jbuf_cstr(b, "{");
    jbuf_key(b, "jsonrpc"); jbuf_str(b, "2.0"); jbuf_cstr(b, ",");
    jbuf_key(b, "id");      jbuf_id(b, id);     jbuf_cstr(b, ",");
    jbuf_key(b, "error");
    jbuf_cstr(b, "{");
    jbuf_key(b, "code");    jbuf_int(b, code);  jbuf_cstr(b, ",");
    jbuf_key(b, "message"); jbuf_str(b, msg);
    jbuf_cstr(b, "}}");
    return (int)b->len;
}

/* ── Tool registry ──────────────────────────────────────────────────────── */

void mcp_registry_init(McpToolRegistry *reg, Arena *root)
{
    reg->count = TOOL_COUNT;

    reg->tools[TOOL_SEARCH_READ] = (McpTool){
        .name        = arena_strdup(root, "search_read_records"),
        .description = arena_strdup(root,
            "Search any Odoo model and return specified fields."),
        .input_schema = arena_strdup(root,
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"model\":{\"type\":\"string\"},"
                "\"domain\":{\"type\":\"array\"},"
                "\"fields\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
                "\"limit\":{\"type\":\"integer\",\"default\":80},"
                "\"offset\":{\"type\":\"integer\",\"default\":0},"
                "\"order\":{\"type\":\"string\"}"
              "},"
              "\"required\":[\"model\"]"
            "}"),
    };

    reg->tools[TOOL_GET_FIELDS] = (McpTool){
        .name        = arena_strdup(root, "get_model_fields"),
        .description = arena_strdup(root,
            "Return field definitions for an Odoo model."),
        .input_schema = arena_strdup(root,
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"model\":{\"type\":\"string\"},"
                "\"attributes\":{\"type\":\"array\","
                  "\"items\":{\"type\":\"string\"}}"
              "},"
              "\"required\":[\"model\"]"
            "}"),
    };

    reg->tools[TOOL_CREATE] = (McpTool){
        .name        = arena_strdup(root, "create_record"),
        .description = arena_strdup(root,
            "Create a new record in an Odoo model."),
        .input_schema = arena_strdup(root,
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"model\":{\"type\":\"string\"},"
                "\"values\":{\"type\":\"object\"}"
              "},"
              "\"required\":[\"model\",\"values\"]"
            "}"),
    };

    reg->tools[TOOL_UPDATE] = (McpTool){
        .name        = arena_strdup(root, "update_record"),
        .description = arena_strdup(root,
            "Update existing records in an Odoo model."),
        .input_schema = arena_strdup(root,
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"model\":{\"type\":\"string\"},"
                "\"record_ids\":{\"type\":\"array\","
                  "\"items\":{\"type\":\"integer\"}},"
                "\"values\":{\"type\":\"object\"}"
              "},"
              "\"required\":[\"model\",\"record_ids\",\"values\"]"
            "}"),
    };
}

/* ── Re-serialise a sj_Value subtree back to JSON ───────────────────────── */
/*
 * sj.h is a non-allocating cursor — it does not build a tree.
 * For tool dispatch we need to pass JSON sub-objects (domain, fields,
 * values, record_ids) through to odoo.c as serialised strings.
 *
 * Strategy: re-scan from the value's start position to its matching
 * close bracket/brace, copying raw bytes.  For scalars, just copy
 * start..end verbatim.
 *
 * We use a fresh sj_Reader from the value's start position and read
 * until depth returns to 0 for containers, or just copy for scalars.
 */
static const char *sj_value_to_json(Arena *a, sj_Value v, const char *src_end)
{
    if (SJ_NULL == v.type)   return "null";
    if (SJ_BOOL == v.type)   return (v.end - v.start == 4) ? "true" : "false";
    if (SJ_NUMBER == v.type) return sj_dup(a, v);
    if (SJ_STRING == v.type) {
        /* v.start/end point inside the quotes — re-add them */
        JsonBuf b = jbuf_new(a, (size_t)(v.end - v.start) + 4);
        jbuf_sj_str(&b, v.start, v.end);
        return b.buf;
    }
    if (SJ_OBJECT == v.type || SJ_ARRAY == v.type) {
        /*
         * v.start points at the '{' or '['.
         * We re-read from that position tracking depth until we close.
         * Because sj_Reader modifies .cur we use a local copy.
         */
        const char *p     = v.start;
        size_t      raw_n = (size_t)(src_end - p);
        /* Scan forward to find the matching close */
        int depth = 0;
        bool in_str = false;
        const char *q = p;
        while (q < src_end) {
            if (in_str) {
                if ('\\' == *q) { q++; }
                else if ('"' == *q) { in_str = false; }
            } else {
                if ('"' == *q) { in_str = true; }
                else if ('{' == *q || '[' == *q) { depth++; }
                else if ('}' == *q || ']' == *q) {
                    depth--;
                    if (0 == depth) { q++; break; }
                }
            }
            q++;
        }
        size_t n = (size_t)(q - p);
        char  *out = (char *)arena_alloc(a, n + 1);
        memcpy(out, p, n);
        out[n] = '\0';
        return out;
    }
    return "null";
}

/* ── tools/call dispatch ────────────────────────────────────────────────── */

static int dispatch_tool(const char    *toolname,
                         sj_Reader     *params_r,   /* positioned after "arguments":{  */
                         sj_Value       args_obj,   /* the arguments object value      */
                         const char    *req_start,  /* original request buffer start   */
                         const char    *req_end,
                         JsonBuf       *out_b,
                         OdooCtx       *ctx,
                         Arena         *a)
{
    char odoo_resp[ODOO_RESP_MAX] = {0};

    /* Extract fields from the arguments object using sj_iter_object */
    const char *model   = NULL;
    const char *args    = "[]";
    const char *kwargs  = "{}";

    /* Collect all key/value pairs we need from arguments */
    sj_Value domain_v   = {0}; bool have_domain   = false;
    sj_Value fields_v   = {0}; bool have_fields    = false;
    sj_Value limit_v    = {0}; bool have_limit     = false;
    sj_Value offset_v   = {0}; bool have_offset    = false;
    sj_Value order_v    = {0}; bool have_order     = false;
    sj_Value values_v   = {0}; bool have_values    = false;
    sj_Value attrs_v    = {0}; bool have_attrs     = false;
    sj_Value ids_v      = {0}; bool have_ids       = false;

    /* Re-parse the arguments object from its raw position */
    sj_Reader ar = sj_reader(args_obj.start,
                             (size_t)(req_end - args_obj.start));
    sj_Value  aobj = sj_read(&ar);
    if (SJ_OBJECT != aobj.type) {
        jbuf_cstr(out_b,
            "{\"isError\":true,\"content\":["
            "{\"type\":\"text\",\"text\":\"arguments must be an object\"}]}");
        return (int)out_b->len;
    }

    sj_Value key, val;
    while (sj_iter_object(&ar, aobj, &key, &val)) {
        if (sj_eq(key, "model") && SJ_STRING == val.type) {
            model = sj_dup(a, val);
        } else if (sj_eq(key, "domain"))     { domain_v  = val; have_domain  = true; }
        else if (sj_eq(key, "fields"))       { fields_v  = val; have_fields  = true; }
        else if (sj_eq(key, "limit"))        { limit_v   = val; have_limit   = true; }
        else if (sj_eq(key, "offset"))       { offset_v  = val; have_offset  = true; }
        else if (sj_eq(key, "order"))        { order_v   = val; have_order   = true; }
        else if (sj_eq(key, "values"))       { values_v  = val; have_values  = true; }
        else if (sj_eq(key, "attributes"))   { attrs_v   = val; have_attrs   = true; }
        else if (sj_eq(key, "record_ids"))   { ids_v     = val; have_ids     = true; }
    }

    if (NULL == model) {
        jbuf_cstr(out_b,
            "{\"isError\":true,\"content\":["
            "{\"type\":\"text\",\"text\":\"missing required field: model\"}]}");
        return (int)out_b->len;
    }

    const char *method = NULL;

    if (0 == strcmp(toolname, "search_read_records")) {
        method = "search_read";
        const char *dom = have_domain
            ? sj_value_to_json(a, domain_v, req_end) : "[]";
        args   = arena_sprintf(a, "[%s]", dom);

        JsonBuf kb = jbuf_new(a, 256);
        jbuf_cstr(&kb, "{");
        jbuf_key(&kb, "fields");
        jbuf_cstr(&kb, have_fields
            ? sj_value_to_json(a, fields_v, req_end)
            : "[\"id\",\"name\"]");
        jbuf_cstr(&kb, ",");
        jbuf_key(&kb, "limit");
        jbuf_int(&kb, have_limit ? sj_ll(limit_v) : 80);
        jbuf_cstr(&kb, ",");
        jbuf_key(&kb, "offset");
        jbuf_int(&kb, have_offset ? sj_ll(offset_v) : 0);
        if (have_order && SJ_STRING == order_v.type) {
            jbuf_cstr(&kb, ",");
            jbuf_key(&kb, "order");
            jbuf_sj_str(&kb, order_v.start, order_v.end);
        }
        jbuf_cstr(&kb, "}");
        kwargs = kb.buf;

    } else if (0 == strcmp(toolname, "get_model_fields")) {
        method = "fields_get";
        args   = "[[]]";
        kwargs = arena_sprintf(a, "{\"attributes\":%s}",
            have_attrs
                ? sj_value_to_json(a, attrs_v, req_end)
                : "[\"string\",\"type\",\"required\"]");

    } else if (0 == strcmp(toolname, "create_record")) {
        method = "create";
        if (!have_values) {
            jbuf_cstr(out_b,
                "{\"isError\":true,\"content\":["
                "{\"type\":\"text\",\"text\":\"missing required field: values\"}]}");
            return (int)out_b->len;
        }
        args   = arena_sprintf(a, "[%s]",
                     sj_value_to_json(a, values_v, req_end));
        kwargs = "{}";

    } else if (0 == strcmp(toolname, "update_record")) {
        method = "write";
        if (!have_ids || !have_values) {
            jbuf_cstr(out_b,
                "{\"isError\":true,\"content\":["
                "{\"type\":\"text\","
                "\"text\":\"missing required fields: record_ids, values\"}]}");
            return (int)out_b->len;
        }
        args   = arena_sprintf(a, "[%s,%s]",
                     sj_value_to_json(a, ids_v,    req_end),
                     sj_value_to_json(a, values_v, req_end));
        kwargs = "{}";

    } else {
        jbuf_cstr(out_b,
            "{\"isError\":true,\"content\":["
            "{\"type\":\"text\",\"text\":\"unknown tool\"}]}");
        return (int)out_b->len;
    }

    int rc = odoo_execute(ctx, model, method, args, kwargs,
                          odoo_resp, sizeof odoo_resp, a);

    /* Wrap XML-RPC response as MCP content block */
    jbuf_cstr(out_b, "{\"content\":[{\"type\":\"text\",\"text\":");
    if (rc < 0) {
        jbuf_str(out_b, "Odoo error — see server log");
    } else {
        jbuf_str(out_b, odoo_resp);
    }
    jbuf_cstr(out_b, "}]}");
    return (int)out_b->len;
}

/* ── mcp_handle ─────────────────────────────────────────────────────────── */

int mcp_handle(const char *req, size_t rlen,
               char *out, size_t olen,
               OdooCtx *ctx,
               const McpToolRegistry *reg,
               Arena *a)
{
    /* sj_reader requires a mutable char* — copy into arena scratch */
    char *buf = (char *)arena_alloc(a, rlen + 1);
    memcpy(buf, req, rlen);
    buf[rlen] = '\0';

    sj_Reader r   = sj_reader(buf, rlen);
    sj_Value  root = sj_read(&r);
    JsonBuf   b    = jbuf_new(a, 4096);

    /* Parse error */
    if (SJ_OBJECT != root.type) {
        const char *e =
            "{\"jsonrpc\":\"2.0\",\"id\":null,"
            "\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}";
        size_t n = strlen(e);
        if (n < olen) memcpy(out, e, n + 1);
        return (int)n;
    }

    /* Extract id, method, params from the top-level object */
    sj_Value id     = { .type = SJ_NULL };
    sj_Value method = { .type = SJ_NULL };
    sj_Value params = { .type = SJ_NULL };

    sj_Value key, val;
    while (sj_iter_object(&r, root, &key, &val)) {
        if (sj_eq(key, "id"))     id     = val;
        if (sj_eq(key, "method")) method = val;
        if (sj_eq(key, "params")) params = val;
    }

    if (SJ_STRING != method.type) {
        jrpc_err(&b, id, -32600, "Invalid Request");
        goto done;
    }

    /* ── initialize ── */
    if (sj_eq(method, "initialize")) {
        const char *result =
            "{"
              "\"protocolVersion\":\"" MCP_PROTO_VERSION "\","
              "\"capabilities\":{\"tools\":{}},"
              "\"serverInfo\":{"
                "\"name\":\"" MCP_SERVER_NAME "\","
                "\"version\":\"" MCP_SERVER_VER "\""
              "}"
            "}";
        jrpc_ok(&b, id, result);
        goto done;
    }

    /* ── initialized (notification — no response) ── */
    if (sj_eq(method, "notifications/initialized") ||
        sj_eq(method, "initialized")) {
        return 0;
    }

    /* ── tools/list ── */
    if (sj_eq(method, "tools/list")) {
        /* Serialise registry into JSON array */
        JsonBuf tb = jbuf_new(a, 4096);
        jbuf_cstr(&tb, "[");
        for (size_t i = 0; i < reg->count; i++) {
            if (i > 0) jbuf_cstr(&tb, ",");
            jbuf_cstr(&tb, "{");
            jbuf_key(&tb, "name");        jbuf_str(&tb, reg->tools[i].name);
            jbuf_cstr(&tb, ",");
            jbuf_key(&tb, "description"); jbuf_str(&tb, reg->tools[i].description);
            jbuf_cstr(&tb, ",");
            jbuf_key(&tb, "inputSchema"); jbuf_cstr(&tb, reg->tools[i].input_schema);
            jbuf_cstr(&tb, "}");
        }
        jbuf_cstr(&tb, "]");
        const char *result = arena_sprintf(a, "{\"tools\":%s}", tb.buf);
        jrpc_ok(&b, id, result);
        goto done;
    }

    /* ── tools/call ── */
    if (sj_eq(method, "tools/call")) {
        if (SJ_OBJECT != params.type) {
            jrpc_err(&b, id, -32602, "Invalid params");
            goto done;
        }

        /* Parse params object for name + arguments */
        sj_Reader pr = sj_reader(params.start,
                                 (size_t)(buf + rlen - params.start));
        sj_Value  pobj = sj_read(&pr);

        sj_Value name_v = { .type = SJ_NULL };
        sj_Value args_v = { .type = SJ_NULL };
        sj_Value pk, pv;
        while (sj_iter_object(&pr, pobj, &pk, &pv)) {
            if (sj_eq(pk, "name"))      name_v = pv;
            if (sj_eq(pk, "arguments")) args_v = pv;
        }

        if (SJ_STRING != name_v.type) {
            jrpc_err(&b, id, -32602, "Invalid params: missing tool name");
            goto done;
        }

        char toolname[64] = {0};
        size_t tn = (size_t)(name_v.end - name_v.start);
        if (tn >= sizeof toolname) tn = sizeof(toolname) - 1;
        memcpy(toolname, name_v.start, tn);

        /* Provide an empty object for arguments if omitted */
        if (SJ_OBJECT != args_v.type) {
            const char *empty = "{}";
            args_v.type  = SJ_OBJECT;
            args_v.start = (char *)empty;
            args_v.end   = (char *)empty + 2;
            args_v.depth = 0;
        }

        JsonBuf tb = jbuf_new(a, 65536);
        dispatch_tool(toolname, &pr, args_v,
                      buf, buf + rlen, &tb, ctx, a);
        jrpc_ok(&b, id, tb.buf);
        goto done;
    }

    /* ── unknown method ── */
    jrpc_err(&b, id, -32601, "Method not found");

done:
    if (b.len >= olen) b.len = olen - 1;
    memcpy(out, b.buf, b.len);
    out[b.len] = '\0';
    return (int)b.len;
}
