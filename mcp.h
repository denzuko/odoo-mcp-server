/*
 * mcp.h — MCP JSON-RPC 2.0 protocol handler
 *
 * Handles: initialize, initialized, tools/list, tools/call
 * Transport-agnostic: caller supplies request body, receives response body.
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * MIT License
 */
#ifndef MCP_H
#define MCP_H

#include <stddef.h>
#include "arena.h"
#include "odoo.h"
#include "config.h"

/*
 * mcp_handle — dispatch one MCP JSON-RPC request.
 *
 * req/rlen — raw request body bytes
 * out/olen — caller buffer for response body
 * ctx      — Odoo connection context
 * a        — arena (reset between requests by caller)
 *
 * Returns byte count written to out, or -1 on internal error.
 * Always writes a valid JSON-RPC response (error object on failure).
 */
int mcp_handle(const char *req, size_t rlen,
               char *out, size_t olen,
               OdooCtx *ctx,
               Arena *a);

#endif /* MCP_H */
