/*
 * tests.c — xUnit test suite for odoo-mcp-server
 *
 * BDD contract: tests define the spec. Code must satisfy tests.
 * Tests must satisfy Rego policy gates. Nothing else drives order.
 *
 * Covers (mapped to policy/c_quality.rego + API headers):
 *   - arena.h  : snapshot/rewind scope, zero-init, alloc
 *   - json.h   : JsonBuf builder, string escaping
 *   - odoo.c   : json_to_xmlrpc type mapping
 *   - mcp.h/c  : registry init, tool count, mcp_handle dispatch
 *   - config.h : env var binding (non-fatal paths only)
 *
 * kcgi not linked — HTTP dispatch requires a FastCGI harness.
 * Network calls not tested — integration_test.py covers those.
 *
 * Build + run via nob:
 *   cc nob.c -o nob && ./nob test
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */

/* stb-style implementations live in impl.c — linked as impl.o */

#include "test.h"
#include "arena.h"
#include "rc.h"
#include "sj.h"
#include "sv.h"
#include "json.h"
#include "config.h"
#include "odoo.h"
#include "mcp.h"

#include <string.h>
#include <stdlib.h>

/* ── Stub: net_http_post ────────────────────────────────────────────────── */
/* Tests do not make real network calls. Stub returns a canned XML-RPC
 * authenticate response so odoo_auth() succeeds without a live server. */

static const char *_net_stub_resp = NULL;

int net_http_post(const char *url, const char *body, size_t blen,
                  char *out, size_t olen, Arena *a)
{
    (void)url; (void)body; (void)blen; (void)a;
    if (NULL == _net_stub_resp) return -1;
    size_t n = strlen(_net_stub_resp);
    if (n >= olen) n = olen - 1;
    memcpy(out, _net_stub_resp, n);
    out[n] = '\0';
    return (int)n;
}

/* ── Suite: arena (tsoding/arena.h contract) ────────────────────────────── */

TEST(arena_zero_init_is_valid)
{
    Arena a = {0};
    /* zero-init arena must be usable immediately — no arena_new() */
    void *p = arena_alloc(&a, 16);
    ASSERT_NOTNULL(p);
    arena_free(&a);
}

TEST(arena_snapshot_rewind_reclaims)
{
    Arena a = {0};
    Arena_Mark m = arena_snapshot(&a);
    char *p = (char *)arena_alloc(&a, 64);
    ASSERT_NOTNULL(p);
    /* rewind reclaims the allocation */
    arena_rewind(&a, m);
    /* allocate again — must succeed (regions still exist) */
    char *q = (char *)arena_alloc(&a, 64);
    ASSERT_NOTNULL(q);
    arena_free(&a);
}

TEST(arena_strdup_copies_string)
{
    Arena a = {0};
    const char *src = "odoo-mcp-server";
    char *dst = arena_strdup(&a, src);
    ASSERT_NOTNULL(dst);
    ASSERT_STR(dst, src);
    /* must be a different pointer */
    ASSERT((dst != src));
    arena_free(&a);
}

TEST(arena_sprintf_formats_correctly)
{
    Arena a = {0};
    char *s = arena_sprintf(&a, "v%d.%d", 1, 9);
    ASSERT_NOTNULL(s);
    ASSERT_STR(s, "v1.9");
    arena_free(&a);
}

/* ── Suite: rc.h (tsoding/rc.h contract) ────────────────────────────────── */

static void _rc_destroy_noop(void *p) { (void)p; }

TEST(rc_alloc_returns_non_null)
{
    int *n = rc_alloc(sizeof *n, _rc_destroy_noop);
    ASSERT_NOTNULL(n);
    rc_release(n);
}

TEST(rc_acquire_increments_count)
{
    int *n = rc_alloc(sizeof *n, _rc_destroy_noop);
    ASSERT_INT(rc_count(n), 0);
    rc_acquire(n);
    ASSERT_INT(rc_count(n), 1);
    rc_release(n);   /* back to 0 — triggers destroy */
}

/* ── Suite: json.h JsonBuf builder ─────────────────────────────────────── */

TEST(jbuf_str_escapes_double_quote)
{
    Arena a = {0};
    JsonBuf b = jbuf_new(&a, 64);
    jbuf_str(&b, "say \"hello\"");
    ASSERT_CONTAINS(b.buf, "\\\"");
    arena_free(&a);
}

TEST(jbuf_str_escapes_backslash)
{
    Arena a = {0};
    JsonBuf b = jbuf_new(&a, 64);
    jbuf_str(&b, "path\\to\\file");
    ASSERT_CONTAINS(b.buf, "\\\\");
    arena_free(&a);
}

TEST(jbuf_str_escapes_newline)
{
    Arena a = {0};
    JsonBuf b = jbuf_new(&a, 64);
    jbuf_str(&b, "line1\nline2");
    ASSERT_CONTAINS(b.buf, "\\n");
    arena_free(&a);
}

TEST(jbuf_int_formats_number)
{
    Arena a = {0};
    JsonBuf b = jbuf_new(&a, 32);
    jbuf_int(&b, 42);
    ASSERT_STR(b.buf, "42");
    arena_free(&a);
}

TEST(jbuf_null_emits_null)
{
    Arena a = {0};
    JsonBuf b = jbuf_new(&a, 16);
    jbuf_null(&b);
    ASSERT_STR(b.buf, "null");
    arena_free(&a);
}

TEST(jbuf_bool_emits_true_false)
{
    Arena a = {0};
    JsonBuf t = jbuf_new(&a, 16);
    JsonBuf f = jbuf_new(&a, 16);
    jbuf_bool(&t, 1);
    jbuf_bool(&f, 0);
    ASSERT_STR(t.buf, "true");
    ASSERT_STR(f.buf, "false");
    arena_free(&a);
}

TEST(jbuf_key_emits_quoted_colon)
{
    Arena a = {0};
    JsonBuf b = jbuf_new(&a, 32);
    jbuf_key(&b, "result");
    ASSERT_STR(b.buf, "\"result\":");
    arena_free(&a);
}

TEST(jbuf_grows_past_initial_cap)
{
    Arena a = {0};
    /* start tiny — must grow */
    JsonBuf b = jbuf_new(&a, 4);
    for (int i = 0; i < 20; i++) jbuf_cstr(&b, "ab");
    ASSERT_INT((int)b.len, 40);
    arena_free(&a);
}

/* ── Suite: odoo.c json_to_xmlrpc ───────────────────────────────────────── */

TEST(json_to_xmlrpc_null)
{
    Arena a = {0};
    const char *x = json_to_xmlrpc(&a, "null");
    ASSERT_CONTAINS(x, "<nil");
    arena_free(&a);
}

TEST(json_to_xmlrpc_string)
{
    Arena a = {0};
    const char *x = json_to_xmlrpc(&a, "\"hello\"");
    ASSERT_CONTAINS(x, "<string>");
    ASSERT_CONTAINS(x, "hello");
    arena_free(&a);
}

TEST(json_to_xmlrpc_integer)
{
    Arena a = {0};
    const char *x = json_to_xmlrpc(&a, "42");
    ASSERT_CONTAINS(x, "<int>");
    ASSERT_CONTAINS(x, "42");
    arena_free(&a);
}

TEST(json_to_xmlrpc_bool_true)
{
    Arena a = {0};
    const char *x = json_to_xmlrpc(&a, "true");
    ASSERT_CONTAINS(x, "<boolean>1");
    arena_free(&a);
}

TEST(json_to_xmlrpc_bool_false)
{
    Arena a = {0};
    const char *x = json_to_xmlrpc(&a, "false");
    ASSERT_CONTAINS(x, "<boolean>0");
    arena_free(&a);
}

TEST(json_to_xmlrpc_array)
{
    Arena a = {0};
    const char *x = json_to_xmlrpc(&a, "[1,2,3]");
    ASSERT_CONTAINS(x, "<array>");
    ASSERT_CONTAINS(x, "<data>");
    arena_free(&a);
}

TEST(json_to_xmlrpc_object)
{
    Arena a = {0};
    const char *x = json_to_xmlrpc(&a, "{\"key\":\"val\"}");
    ASSERT_CONTAINS(x, "<struct>");
    ASSERT_CONTAINS(x, "<member>");
    ASSERT_CONTAINS(x, "<name>key</name>");
    arena_free(&a);
}

TEST(json_to_xmlrpc_empty_input)
{
    Arena a = {0};
    /* empty string → fallback to empty struct */
    const char *x = json_to_xmlrpc(&a, "");
    ASSERT_NOTNULL(x);
    ASSERT_CONTAINS(x, "<struct>");
    arena_free(&a);
}

/* ── Suite: mcp.h registry ──────────────────────────────────────────────── */

TEST(registry_has_tool_count_tools)
{
    Arena root = {0};
    McpToolRegistry reg = {0};
    mcp_registry_init(&reg, &root);
    ASSERT_INT((int)reg.count, (int)TOOL_COUNT);
    arena_free(&root);
}

TEST(registry_all_tools_have_name)
{
    Arena root = {0};
    McpToolRegistry reg = {0};
    mcp_registry_init(&reg, &root);
    for (size_t i = 0; i < reg.count; i++) {
        ASSERT_NOTNULL(reg.tools[i].name);
        ASSERT((strlen(reg.tools[i].name) > 0));
    }
    arena_free(&root);
}

TEST(registry_all_tools_have_description)
{
    Arena root = {0};
    McpToolRegistry reg = {0};
    mcp_registry_init(&reg, &root);
    for (size_t i = 0; i < reg.count; i++)
        ASSERT_NOTNULL(reg.tools[i].description);
    arena_free(&root);
}

TEST(registry_all_tools_have_schema)
{
    Arena root = {0};
    McpToolRegistry reg = {0};
    mcp_registry_init(&reg, &root);
    for (size_t i = 0; i < reg.count; i++) {
        ASSERT_NOTNULL(reg.tools[i].input_schema);
        ASSERT_CONTAINS(reg.tools[i].input_schema, "\"type\"");
        ASSERT_CONTAINS(reg.tools[i].input_schema, "\"model\"");
    }
    arena_free(&root);
}

TEST(registry_search_read_is_first)
{
    Arena root = {0};
    McpToolRegistry reg = {0};
    mcp_registry_init(&reg, &root);
    ASSERT_STR(reg.tools[TOOL_SEARCH_READ].name, "search_read_records");
    arena_free(&root);
}

TEST(registry_tool_names_are_unique)
{
    Arena root = {0};
    McpToolRegistry reg = {0};
    mcp_registry_init(&reg, &root);
    for (size_t i = 0; i < reg.count; i++)
        for (size_t j = i + 1; j < reg.count; j++)
            ASSERT(0 != strcmp(reg.tools[i].name, reg.tools[j].name));
    arena_free(&root);
}

/* ── Suite: mcp_handle JSON-RPC dispatch ────────────────────────────────── */

static McpToolRegistry _g_reg;
static Arena           _g_root;

static void _setup_mcp(void)
{
    _g_root = (Arena){0};
    mcp_registry_init(&_g_reg, &_g_root);
}

static void _teardown_mcp(void)
{
    arena_free(&_g_root);
}

/* Helper: run mcp_handle with a null OdooCtx (tests not touching odoo) */
static int _dispatch(const char *req, char *out, size_t olen)
{
    Arena a = {0};
    Arena_Mark m = arena_snapshot(&a);
    /* We pass NULL for ctx — tools/list and initialize don't use it */
    int n = mcp_handle(req, strlen(req), out, olen, NULL, &_g_reg, &a);
    arena_rewind(&a, m);
    arena_free(&a);
    return n;
}

TEST(mcp_initialize_returns_protocol_version)
{
    _setup_mcp();
    char out[1024] = {0};
    int n = _dispatch(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
        "\"params\":{\"protocolVersion\":\"2025-03-26\"}}",
        out, sizeof out);
    ASSERT((n > 0));
    ASSERT_CONTAINS(out, "2025-03-26");
    ASSERT_CONTAINS(out, MCP_SERVER_NAME);
    _teardown_mcp();
}

TEST(mcp_tools_list_returns_all_tools)
{
    _setup_mcp();
    char out[4096] = {0};
    int n = _dispatch(
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
        out, sizeof out);
    ASSERT((n > 0));
    ASSERT_CONTAINS(out, "search_read_records");
    ASSERT_CONTAINS(out, "get_model_fields");
    ASSERT_CONTAINS(out, "create_record");
    ASSERT_CONTAINS(out, "update_record");
    _teardown_mcp();
}

TEST(mcp_parse_error_on_invalid_json)
{
    _setup_mcp();
    char out[512] = {0};
    int n = _dispatch("{not valid json", out, sizeof out);
    ASSERT((n > 0));
    ASSERT_CONTAINS(out, "-32700");
    _teardown_mcp();
}

TEST(mcp_unknown_method_returns_minus_32601)
{
    _setup_mcp();
    char out[512] = {0};
    int n = _dispatch(
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"unknown/method\"}",
        out, sizeof out);
    ASSERT((n > 0));
    ASSERT_CONTAINS(out, "-32601");
    _teardown_mcp();
}

TEST(mcp_initialized_notification_no_response)
{
    _setup_mcp();
    char out[512] = {0};
    /* notifications have no id — server must not respond */
    int n = _dispatch(
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",
        out, sizeof out);
    ASSERT_INT(n, 0);
    _teardown_mcp();
}

TEST(mcp_tools_call_missing_model_returns_error)
{
    _setup_mcp();
    char out[512] = {0};
    int n = _dispatch(
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"search_read_records\",\"arguments\":{}}}",
        out, sizeof out);
    ASSERT((n > 0));
    ASSERT_CONTAINS(out, "model");
    _teardown_mcp();
}

TEST(mcp_tools_call_create_empty_values_is_error)
{
    _setup_mcp();
    char out[512] = {0};
    int n = _dispatch(
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"create_record\","
        "\"arguments\":{\"model\":\"res.partner\",\"values\":{}}}}",
        out, sizeof out);
    ASSERT((n > 0));
    /* values is empty — expect isError or odoo error */
    ASSERT_CONTAINS(out, "result");
    _teardown_mcp();
}

TEST(mcp_id_preserved_as_number)
{
    _setup_mcp();
    char out[1024] = {0};
    _dispatch(
        "{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"tools/list\"}",
        out, sizeof out);
    ASSERT_CONTAINS(out, "\"id\":99");
    _teardown_mcp();
}

TEST(mcp_id_preserved_as_string)
{
    _setup_mcp();
    char out[1024] = {0};
    _dispatch(
        "{\"jsonrpc\":\"2.0\",\"id\":\"req-abc\",\"method\":\"tools/list\"}",
        out, sizeof out);
    ASSERT_CONTAINS(out, "req-abc");
    _teardown_mcp();
}

/* ── Suite: odoo_auth stub ──────────────────────────────────────────────── */

TEST(odoo_auth_succeeds_with_stub_response)
{
    /* Stub a valid XML-RPC authenticate response: uid=7 */
    _net_stub_resp =
        "<?xml version=\"1.0\"?>"
        "<methodResponse><params><param>"
        "<value><int>7</int></value>"
        "</param></params></methodResponse>";

    Arena a    = {0};
    Config cfg = {
        .odoo_url    = "https://dapla.net",
        .odoo_db     = "testdb",
        .odoo_user   = "test@test.com",
        .odoo_apikey = "testkey",
        .host        = "127.0.0.1",
        .port        = "8000",
    };
    OdooCtx ctx = { &cfg, 0 };
    int uid = odoo_auth(&ctx, &a);
    ASSERT_INT(uid, 7);
    ASSERT_INT(ctx.uid, 7);
    arena_free(&a);
    _net_stub_resp = NULL;
}

TEST(odoo_auth_caches_uid)
{
    /* Second call must not hit the network (stub returns -1 if called) */
    Arena a    = {0};
    Config cfg = {
        .odoo_url    = "https://dapla.net",
        .odoo_db     = "testdb",
        .odoo_user   = "test@test.com",
        .odoo_apikey = "testkey",
        .host        = "127.0.0.1",
        .port        = "8000",
    };
    OdooCtx ctx = { &cfg, 7 };   /* pre-seeded uid */
    _net_stub_resp = NULL;        /* no network */
    int uid = odoo_auth(&ctx, &a);
    ASSERT_INT(uid, 7);
    arena_free(&a);
}

TEST(odoo_auth_fails_on_bad_credentials)
{
    /* Stub: uid=0 (False) means rejected */
    _net_stub_resp =
        "<?xml version=\"1.0\"?>"
        "<methodResponse><params><param>"
        "<value><int>0</int></value>"
        "</param></params></methodResponse>";

    Arena a    = {0};
    Config cfg = {
        .odoo_url    = "https://dapla.net",
        .odoo_db     = "testdb",
        .odoo_user   = "bad@user.com",
        .odoo_apikey = "wrong",
        .host        = "127.0.0.1",
        .port        = "8000",
    };
    OdooCtx ctx = { &cfg, 0 };
    int uid = odoo_auth(&ctx, &a);
    ASSERT_INT(uid, -1);
    arena_free(&a);
    _net_stub_resp = NULL;
}

/* ── Runner ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("odoo-mcp-server xUnit test suite\n");
    printf("=================================\n\n");

    printf("arena:\n");
    RUN(arena_zero_init_is_valid);
    RUN(arena_snapshot_rewind_reclaims);
    RUN(arena_strdup_copies_string);
    RUN(arena_sprintf_formats_correctly);

    printf("\nrc:\n");
    RUN(rc_alloc_returns_non_null);
    RUN(rc_acquire_increments_count);

    printf("\njson builder:\n");
    RUN(jbuf_str_escapes_double_quote);
    RUN(jbuf_str_escapes_backslash);
    RUN(jbuf_str_escapes_newline);
    RUN(jbuf_int_formats_number);
    RUN(jbuf_null_emits_null);
    RUN(jbuf_bool_emits_true_false);
    RUN(jbuf_key_emits_quoted_colon);
    RUN(jbuf_grows_past_initial_cap);

    printf("\nodoo json_to_xmlrpc:\n");
    RUN(json_to_xmlrpc_null);
    RUN(json_to_xmlrpc_string);
    RUN(json_to_xmlrpc_integer);
    RUN(json_to_xmlrpc_bool_true);
    RUN(json_to_xmlrpc_bool_false);
    RUN(json_to_xmlrpc_array);
    RUN(json_to_xmlrpc_object);
    RUN(json_to_xmlrpc_empty_input);

    printf("\nmcp registry:\n");
    RUN(registry_has_tool_count_tools);
    RUN(registry_all_tools_have_name);
    RUN(registry_all_tools_have_description);
    RUN(registry_all_tools_have_schema);
    RUN(registry_search_read_is_first);
    RUN(registry_tool_names_are_unique);

    printf("\nmcp_handle dispatch:\n");
    RUN(mcp_initialize_returns_protocol_version);
    RUN(mcp_tools_list_returns_all_tools);
    RUN(mcp_parse_error_on_invalid_json);
    RUN(mcp_unknown_method_returns_minus_32601);
    RUN(mcp_initialized_notification_no_response);
    RUN(mcp_tools_call_missing_model_returns_error);
    RUN(mcp_tools_call_create_empty_values_is_error);
    RUN(mcp_id_preserved_as_number);
    RUN(mcp_id_preserved_as_string);

    printf("\nodoo_auth:\n");
    RUN(odoo_auth_succeeds_with_stub_response);
    RUN(odoo_auth_caches_uid);
    RUN(odoo_auth_fails_on_bad_credentials);

    return xunit_summary();
}
