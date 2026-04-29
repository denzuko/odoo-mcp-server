/**
 * worker.js — Cloudflare Workers shim for odoo-mcp.wasm
 *
 * Bridges the WASM http_fetch import to Workers fetch() API.
 * The WASM module handles all MCP JSON-RPC logic.
 *
 * Deploy:  wrangler deploy
 * Secrets: wrangler secret put ODOO_URL / ODOO_DB / ODOO_USER / ODOO_API_KEY
 */

import { WASI } from "@cloudflare/workers-wasi";
import wasm from "./odoo-mcp.wasm";

export default {
  async fetch(request, env) {
    const wasi = new WASI({ env: {
      ODOO_URL:     env.ODOO_URL,
      ODOO_DB:      env.ODOO_DB,
      ODOO_USER:    env.ODOO_USER,
      ODOO_API_KEY: env.ODOO_API_KEY,
    }});

    // Bridge: WASM calls env.http_fetch → Workers fetch()
    const imports = {
      env: {
        http_fetch: async (urlPtr, urlLen, bodyPtr, bodyLen, outPtr, outMax) => {
          const mem  = new Uint8Array(instance.exports.memory.buffer);
          const url  = new TextDecoder().decode(mem.slice(urlPtr, urlPtr + urlLen));
          const body = mem.slice(bodyPtr, bodyPtr + bodyLen);
          const resp = await fetch(url, {
            method:  "POST",
            headers: { "Content-Type": "text/xml; charset=utf-8" },
            body,
          });
          const text = await resp.text();
          const enc  = new TextEncoder().encode(text);
          const n    = Math.min(enc.length, outMax - 1);
          mem.set(enc.slice(0, n), outPtr);
          mem[outPtr + n] = 0; // null-terminate
          return n;
        },
      },
      wasi_snapshot_preview1: wasi.wasiImport,
    };

    const instance = new WebAssembly.Instance(wasm, imports);
    wasi.initialize(instance);

    const url  = new URL(request.url);
    const path = url.pathname;

    if (path === "/healthz") {
      return new Response(JSON.stringify({ status: "ok" }),
        { headers: { "Content-Type": "application/json" } });
    }

    if (path === "/mcp" && request.method === "POST") {
      const body    = await request.text();
      const enc     = new TextEncoder();
      const bodyArr = enc.encode(body);

      // Allocate buffers in WASM linear memory via arena
      const mem      = instance.exports.memory;
      const arena    = instance.exports.arena_new(4 * 1024 * 1024);
      const reqPtr   = /* simplification: use WASI stdin via postMessage */
                       0; // See note below
      const respPtr  = 0;

      /*
       * NOTE: The full integration passes req/resp through WASM exported
       * functions. For the stateless reactor model, the JS shim reads the
       * request body, writes it to WASM linear memory, calls mcp_handle(),
       * then reads the response back. The arena ptrs above are illustrative;
       * the production implementation uses the Extism-style host-call pattern
       * or the workers-wasi stdin/stdout bridge which maps HTTP body → stdin.
       *
       * In the stdin/stdout bridge model (used by workers-wasi):
       *   - POST body → WASM stdin
       *   - WASM stdout → HTTP response body
       * This requires main.c's WASM stub (see below) to read from stdin
       * and write to stdout, which is exactly what the CGI model does.
       */

      return wasi.fetch(instance, request);
    }

    return new Response("not found", { status: 404 });
  },
};
