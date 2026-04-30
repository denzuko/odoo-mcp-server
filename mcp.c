/*
 * mcp.c — MCP JSON-RPC 2.0 dispatcher
 *
 * Wire format (2025-03-26 spec):
 *   POST /mcp  Content-Type: application/json
 *   Body: {"jsonrpc":"2.0","id":N,"method":"...","params":{...}}
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#include "mcp.h"
#include "json.h"
#include "odoo.h"
#include "arena.h"
#include "sv.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

/* ── JSON-RPC response helpers ──────────────────────────────────────────── */

static int jrpc_ok(JsonBuf *b, const JsonVal *id, const char *result_json)
{
    jbuf_cstr(b, "{");
    jbuf_key(b, "jsonrpc"); jbuf_str(b, "2.0"); jbuf_cstr(b, ",");
    jbuf_key(b, "id");
    if (NULL == id || JSON_NULL == id->type) {
        jbuf_null(b);
    } else if (JSON_NUMBER == id->type) {
        jbuf_int(b, (long long)id->u.n);
    } else if (JSON_STRING == id->type) {
        jbuf_sv(b, id->u.s);
    } else {
        jbuf_null(b);
    }
    jbuf_cstr(b, ",");
    jbuf_key(b, "result");
    jbuf_cstr(b, result_json);   /* already-serialised result */
    jbuf_cstr(b, "}");
    return (int)b->len;
}

static int jrpc_err(JsonBuf *b, const JsonVal *id,
                    int code, const char *msg)
{
    jbuf_cstr(b, "{");
    jbuf_key(b, "jsonrpc"); jbuf_str(b, "2.0"); jbuf_cstr(b, ",");
    jbuf_key(b, "id");
    if (NULL == id || JSON_NULL == id->type) jbuf_null(b);
    else if (JSON_NUMBER == id->type) jbuf_int(b, (long long)id->u.n);
    else jbuf_null(b);
    jbuf_cstr(b, ",");
    jbuf_key(b, "error"); jbuf_cstr(b, "{");
    jbuf_key(b, "code");    jbuf_int(b, code); jbuf_cstr(b, ",");
    jbuf_key(b, "message"); jbuf_str(b, msg);
    jbuf_cstr(b, "}}");
    return (int)b->len;
}

/* ── Tool schemas (tools/list) ──────────────────────────────────────────── */

static const char TOOLS_LIST[] =
"["
  "{"
    "\"name\":\"search_read_records\","
    "\"description\":\"Search any Odoo model and return specified fields.\","
    "\"inputSchema\":{"
      "\"type\":\"object\","
      "\"properties\":{"
        "\"model\":{\"type\":\"string\",\"description\":\"Odoo model name e.g. res.partner\"},"
        "\"domain\":{\"type\":\"array\",\"description\":\"Odoo domain filter list\"},"
        "\"fields\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
        "\"limit\":{\"type\":\"integer\",\"default\":80},"
        "\"offset\":{\"type\":\"integer\",\"default\":0},"
        "\"order\":{\"type\":\"string\"}"
      "},"
      "\"required\":[\"model\"]"
    "}"
  "},"
  "{"
    "\"name\":\"get_model_fields\","
    "\"description\":\"Return field definitions for an Odoo model.\","
    "\"inputSchema\":{"
      "\"type\":\"object\","
      "\"properties\":{"
        "\"model\":{\"type\":\"string\"},"
        "\"attributes\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}"
      "},"
      "\"required\":[\"model\"]"
    "}"
  "},"
  "{"
    "\"name\":\"create_record\","
    "\"description\":\"Create a new record in an Odoo model.\","
    "\"inputSchema\":{"
      "\"type\":\"object\","
      "\"properties\":{"
        "\"model\":{\"type\":\"string\"},"
        "\"values\":{\"type\":\"object\"}"
      "},"
      "\"required\":[\"model\",\"values\"]"
    "}"
  "},"
  "{"
    "\"name\":\"update_record\","
    "\"description\":\"Update existing records in an Odoo model.\","
    "\"inputSchema\":{"
      "\"type\":\"object\","
      "\"properties\":{"
        "\"model\":{\"type\":\"string\"},"
        "\"record_ids\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"}},"
        "\"values\":{\"type\":\"object\"}"
      "},"
      "\"required\":[\"model\",\"record_ids\",\"values\"]"
    "}"
  "}"
"]";

/* ── tools/call dispatch ────────────────────────────────────────────────── */

/*
 * Extract a JSON sub-value as a freshly-serialised string.
 * Returns "{}" if not found, so callers always get valid JSON.
 */
static const char *jval_to_json(Arena *a, const JsonVal *v)
{
    if (NULL == v) return "{}";
    /* Re-serialise using JsonBuf */
    JsonBuf b = jbuf_new(a, 4096);
    switch (v->type) {
    case JSON_NULL:   jbuf_null(&b);                break;
    case JSON_BOOL:   jbuf_bool(&b, v->u.b);        break;
    case JSON_NUMBER: jbuf_int(&b, (long long)v->u.n); break;
    case JSON_STRING: jbuf_sv(&b, v->u.s);           break;
    case JSON_ARRAY: {
        jbuf_cstr(&b, "[");
        bool first = true;
        for (const JsonVal *item = v->u.items; item; item = item->next) {
            if (!first) jbuf_cstr(&b, ",");
            jbuf_cstr(&b, jval_to_json(a, item));
            first = false;
        }
        jbuf_cstr(&b, "]");
        break;
    }
    case JSON_OBJECT: {
        jbuf_cstr(&b, "{");
        bool first = true;
        for (const JsonPair *pr = v->u.pairs; pr; pr = pr->next) {
            if (!first) jbuf_cstr(&b, ",");
            jbuf_sv(&b, pr->key);
            jbuf_cstr(&b, ":");
            jbuf_cstr(&b, jval_to_json(a, pr->val));
            first = false;
        }
        jbuf_cstr(&b, "}");
        break;
    }
    default: jbuf_cstr(&b, "null"); break;
    }
    return b.buf;
}

/*
 * Wrap an Odoo XML-RPC response (raw XML) in a MCP content block.
 * We return it as a text block — the LLM can parse or describe the XML.
 */
static const char *wrap_odoo_result(Arena *a, const char *xml_resp, int rc)
{
    if (rc < 0 || NULL == xml_resp)
        return "{\"type\":\"text\",\"text\":\"Odoo error — see server log\"}";

    JsonBuf b = jbuf_new(a, 512 + (size_t)rc);
    jbuf_cstr(&b, "{");
    jbuf_key(&b, "type"); jbuf_str(&b, "text"); jbuf_cstr(&b, ",");
    jbuf_key(&b, "text"); jbuf_str(&b, xml_resp);
    jbuf_cstr(&b, "}");
    return b.buf;
}

static int dispatch_tool(const char *name, const JsonVal *input_obj,
                         JsonBuf *out_b, OdooCtx *ctx, Arena *a)
{
    char odoo_resp[ODOO_RESP_MAX] = {0};
    const char *model_s = NULL;
    {
        const JsonVal *m = json_get(input_obj, "model");
        if (NULL == m || JSON_STRING != m->type) {
            jbuf_cstr(out_b, "{\"isError\":true,\"content\":["
                "{\"type\":\"text\",\"text\":\"missing required field: model\"}]}");
            return (int)out_b->len;
        }
        model_s = arena_strdup(a, "");
        (void)model_s;
        /* extract as c-string */
        char *ms = (char *)arena_alloc(a, m->u.s.len + 1);
        memcpy(ms, m->u.s.data, m->u.s.len);
        ms[m->u.s.len] = '\0';
        model_s = ms;
    }

    const char *args   = "[]";
    const char *kwargs = "{}";

    if (0 == strcmp(name, "search_read_records")) {
        /* args: [domain]  kwargs: {fields, limit, offset, order} */
        const JsonVal *domain = json_get(input_obj, "domain");
        args = domain ? jval_to_json(a, domain) : "[[]]";
        /* Wrap in outer array */
        args = arena_sprintf(a, "[%s]", args);

        JsonBuf kb = jbuf_new(a, 512);
        jbuf_cstr(&kb, "{");

        const JsonVal *fields = json_get(input_obj, "fields");
        jbuf_key(&kb, "fields");
        jbuf_cstr(&kb, fields ? jval_to_json(a, fields) : "[\"id\",\"name\"]");

        const JsonVal *limit = json_get(input_obj, "limit");
        jbuf_cstr(&kb, ","); jbuf_key(&kb, "limit");
        jbuf_int(&kb, limit ? (long long)limit->u.n : 80);

        const JsonVal *offset = json_get(input_obj, "offset");
        jbuf_cstr(&kb, ","); jbuf_key(&kb, "offset");
        jbuf_int(&kb, offset ? (long long)offset->u.n : 0);

        const JsonVal *order = json_get(input_obj, "order");
        if (order && JSON_STRING == order->type) {
            jbuf_cstr(&kb, ","); jbuf_key(&kb, "order");
            jbuf_sv(&kb, order->u.s);
        }
        jbuf_cstr(&kb, "}");
        kwargs = kb.buf;

    } else if (0 == strcmp(name, "get_model_fields")) {
        args   = "[[]]";
        const JsonVal *attrs = json_get(input_obj, "attributes");
        kwargs = arena_sprintf(a, "{\"attributes\":%s}",
                    attrs ? jval_to_json(a, attrs)
                          : "[\"string\",\"type\",\"required\"]");

    } else if (0 == strcmp(name, "create_record")) {
        const JsonVal *vals = json_get(input_obj, "values");
        if (NULL == vals) {
            jbuf_cstr(out_b, "{\"isError\":true,\"content\":["
                "{\"type\":\"text\",\"text\":\"missing required field: values\"}]}");
            return (int)out_b->len;
        }
        args   = arena_sprintf(a, "[%s]", jval_to_json(a, vals));
        kwargs = "{}";

    } else if (0 == strcmp(name, "update_record")) {
        const JsonVal *ids  = json_get(input_obj, "record_ids");
        const JsonVal *vals = json_get(input_obj, "values");
        if (NULL == ids || NULL == vals) {
            jbuf_cstr(out_b, "{\"isError\":true,\"content\":["
                "{\"type\":\"text\","
                "\"text\":\"missing required fields: record_ids, values\"}]}");
            return (int)out_b->len;
        }
        args   = arena_sprintf(a, "[%s,%s]",
                    jval_to_json(a, ids), jval_to_json(a, vals));
        kwargs = "{}";

    } else {
        jbuf_cstr(out_b, "{\"isError\":true,\"content\":["
            "{\"type\":\"text\",\"text\":\"unknown tool\"}]}");
        return (int)out_b->len;
    }

    int rc = odoo_execute(ctx, model_s,
                          /* method */ (0 == strcmp(name, "search_read_records"))
                              ? "search_read"
                              : (0 == strcmp(name, "get_model_fields"))
                              ? "fields_get"
                              : (0 == strcmp(name, "create_record"))
                              ? "create"
                              : "write",
                          args, kwargs,
                          odoo_resp, sizeof odoo_resp, a);

    const char *content = wrap_odoo_result(a, odoo_resp, rc);
    jbuf_cstr(out_b, "{\"content\":[");
    jbuf_cstr(out_b, content);
    jbuf_cstr(out_b, "]}");
    return (int)out_b->len;
}

/* ── mcp_handle ─────────────────────────────────────────────────────────── */

int mcp_handle(const char *req, size_t rlen,
               char *out, size_t olen,
               OdooCtx *ctx,
               Arena *a)
{
    char errbuf[128] = {0};
    JsonVal *root = json_parse(req, rlen, a, errbuf, sizeof errbuf);
    if (NULL == root) {
        const char *e = "{\"jsonrpc\":\"2.0\",\"id\":null,"
                        "\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}";
        size_t n = strlen(e);
        if (n < olen) memcpy(out, e, n + 1);
        return (int)n;
    }

    const JsonVal *id     = json_get(root, "id");
    const JsonVal *method = json_get(root, "method");
    const JsonVal *params = json_get(root, "params");

    JsonBuf b = jbuf_new(a, 8192);

    if (NULL == method || JSON_STRING != method->type) {
        jrpc_err(&b, id, -32600, "Invalid Request");
        goto done;
    }

    Sv m = method->u.s;

    /* initialize */
    if (sv_eq(m, SV_LIT("initialize"))) {
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

    /* initialized — notification, no response */
    if (sv_eq(m, SV_LIT("notifications/initialized")) ||
        sv_eq(m, SV_LIT("initialized"))) {
        /* spec: server MUST NOT reply to notifications */
        return 0;
    }

    /* tools/list */
    if (sv_eq(m, SV_LIT("tools/list"))) {
        const char *result = arena_sprintf(a, "{\"tools\":%s}", TOOLS_LIST);
        jrpc_ok(&b, id, result);
        goto done;
    }

    /* tools/call */
    if (sv_eq(m, SV_LIT("tools/call"))) {
        if (NULL == params) {
            jrpc_err(&b, id, -32602, "Invalid params: missing params");
            goto done;
        }
        const JsonVal *name_v  = json_get(params, "name");
        const JsonVal *input_v = json_get(params, "arguments");

        if (NULL == name_v || JSON_STRING != name_v->type) {
            jrpc_err(&b, id, -32602, "Invalid params: missing tool name");
            goto done;
        }

        char toolname[64] = {0};
        size_t tn = name_v->u.s.len < 63 ? name_v->u.s.len : 63;
        memcpy(toolname, name_v->u.s.data, tn);

        JsonBuf tb = jbuf_new(a, 65536);
        dispatch_tool(toolname,
                      input_v && JSON_OBJECT == input_v->type ? input_v : NULL,
                      &tb, ctx, a);

        jrpc_ok(&b, id, tb.buf);
        goto done;
    }

    /* Unknown method */
    jrpc_err(&b, id, -32601, "Method not found");

done:
    if (b.len >= olen) b.len = olen - 1;
    memcpy(out, b.buf, b.len);
    out[b.len] = '\0';
    return (int)b.len;
}
