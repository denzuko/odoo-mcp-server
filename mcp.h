/*
 * mcp.h — MCP JSON-RPC 2.0 protocol handler
 *
 * Handles: initialize, initialized, tools/list, tools/call
 * Transport-agnostic: caller supplies request body, receives response body.
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#ifndef MCP_H
#define MCP_H

#include <stddef.h>
#include "arena.h"
#include "odoo.h"
#include "config.h"

/* ── Tool registry ──────────────────────────────────────────────────────── */

typedef enum {
    TOOL_SEARCH_READ = 0,
    TOOL_GET_FIELDS,
    TOOL_CREATE,
    TOOL_UPDATE,
    TOOL_COUNT         /* sentinel — always last */
} McpToolId;

typedef struct {
    const char *name;          /* e.g. "search_read_records" */
    const char *description;
    const char *input_schema;  /* JSON Schema object as C string */
} McpTool;

typedef struct {
    McpTool    tools[TOOL_COUNT];
    size_t     count;
} McpToolRegistry;

/*
 * mcp_registry_init — populate the tool registry into the root arena.
 * Call once at startup, before any mcp_handle() calls.
 */
void mcp_registry_init(McpToolRegistry *reg, Arena *root);

/*
 * mcp_handle — dispatch one MCP JSON-RPC request.
 *
 * req/rlen — raw request body bytes
 * out/olen — caller buffer for response body
 * ctx      — Odoo connection context
 * reg      — tool registry (populated by mcp_registry_init)
 * a        — per-request arena (reset between requests by caller)
 *
 * Returns byte count written to out, or -1 on internal error.
 * Always writes a valid JSON-RPC response (error object on failure).
 */
int mcp_handle(const char *req, size_t rlen,
               char *out, size_t olen,
               OdooCtx *ctx,
               const McpToolRegistry *reg,
               Arena *a);

#ifdef __wasm__
/* WASM reactor exports — called by workers/worker.js */
void  mcp_init(void);
void *mcp_alloc(size_t n);
int   mcp_handle_wasm(const char *req, size_t rlen, char *out, size_t olen);
#endif

#endif /* MCP_H */
