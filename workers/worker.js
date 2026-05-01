/**
 * worker.js — Cloudflare Workers shim for odoo-mcp.wasm
 *
 * No npm dependencies. Uses native WebAssembly APIs only.
 *
 * The WASM module is built with:
 *   --target=wasm32-wasi -mexec-model=reactor
 *   -Wl,--export=mcp_handle_wasm
 *
 * mcp_handle() is a reactor export — it is called directly.
 * The WASM module imports http_fetch from the host (this shim).
 *
 * Deploy: wrangler deploy
 */

import wasm from "./odoo-mcp.wasm";

// Encode/decode helpers
const enc = new TextEncoder();
const dec = new TextDecoder();

// Shared WASM instance (reactor model — one instance per isolate)
let instance = null;

async function getInstance(env) {
  if (instance) return instance;

  const imports = {
    env: {
      /**
       * http_fetch — called by net.c in the WASM module.
       * Bridges WASM XML-RPC calls to Workers fetch().
       *
       * Signature (C): int http_fetch(
       *   const char *url, const char *body, size_t blen,
       *   char *out, size_t olen, Arena *a)
       */
      http_fetch: async (urlPtr, urlLen, bodyPtr, bodyLen, outPtr, outMax) => {
        const mem  = new Uint8Array(instance.exports.memory.buffer);
        const url  = dec.decode(mem.slice(urlPtr, urlPtr + urlLen));
        const body = mem.slice(bodyPtr, bodyPtr + bodyLen);

        const resp = await fetch(url, {
          method:  "POST",
          headers: { "Content-Type": "text/xml; charset=utf-8" },
          body,
        });
        const text = await resp.text();
        const out  = enc.encode(text);
        const n    = Math.min(out.length, outMax - 1);
        mem.set(out.slice(0, n), outPtr);
        mem[outPtr + n] = 0;
        return n;
      },
    },
    // Minimal WASI snapshot — reactor model only needs proc_exit
    wasi_snapshot_preview1: {
      proc_exit: (code) => { throw new Error("wasi proc_exit: " + code); },
      fd_write:  ()     => 0,
      fd_read:   ()     => 0,
      fd_seek:   ()     => 0,
      fd_close:  ()     => 0,
      environ_sizes_get: (countPtr, sizePtr) => {
        const mem = new DataView(instance.exports.memory.buffer);
        const vars = [
          "ODOO_URL="     + (env.ODOO_URL     || ""),
          "ODOO_DB="      + (env.ODOO_DB      || ""),
          "ODOO_USER="    + (env.ODOO_USER    || ""),
          "ODOO_API_KEY=" + (env.ODOO_API_KEY || ""),
        ];
        mem.setUint32(countPtr, vars.length, true);
        mem.setUint32(sizePtr,
          vars.reduce((a, s) => a + enc.encode(s).length + 1, 0), true);
        return 0;
      },
      environ_get: (environPtr, bufPtr) => {
        const mem = new DataView(instance.exports.memory.buffer);
        const buf = new Uint8Array(instance.exports.memory.buffer);
        const vars = [
          "ODOO_URL="     + (env.ODOO_URL     || ""),
          "ODOO_DB="      + (env.ODOO_DB      || ""),
          "ODOO_USER="    + (env.ODOO_USER    || ""),
          "ODOO_API_KEY=" + (env.ODOO_API_KEY || ""),
        ];
        let off = bufPtr;
        for (let i = 0; i < vars.length; i++) {
          mem.setUint32(environPtr + i * 4, off, true);
          const bytes = enc.encode(vars[i]);
          buf.set(bytes, off);
          buf[off + bytes.length] = 0;
          off += bytes.length + 1;
        }
        return 0;
      },
      args_sizes_get: (cp, sp) => {
        const mem = new DataView(instance.exports.memory.buffer);
        mem.setUint32(cp, 0, true);
        mem.setUint32(sp, 0, true);
        return 0;
      },
      args_get: () => 0,
      clock_time_get: (id, prec, timePtr) => {
        const mem = new DataView(instance.exports.memory.buffer);
        const now = BigInt(Date.now()) * 1_000_000n;
        mem.setBigUint64(timePtr, now, true);
        return 0;
      },
    },
  };

  const mod = await WebAssembly.instantiate(wasm, imports);
  instance = mod.instance;
  // Reactor: call mcp_init once to set up arena and registry
  if (instance.exports.mcp_init) instance.exports.mcp_init();
  return instance;
}

export default {
  async fetch(request, env) {
    const url  = new URL(request.url);

    if (url.pathname === "/healthz") {
      return new Response(JSON.stringify({ status: "ok", service: "odoo-mcp-server" }),
        { headers: { "Content-Type": "application/json" } });
    }

    if (url.pathname !== "/mcp" || request.method !== "POST") {
      return new Response("not found", { status: 404 });
    }

    try {
      const inst = await getInstance(env);

      // Write request body into WASM linear memory
      const body    = await request.text();
      const bodyArr = enc.encode(body);
      const reqLen  = bodyArr.length;
      const respMax = 512 * 1024;

      // Allocate scratch in WASM memory via exported arena
      const reqPtr  = inst.exports.mcp_alloc(reqLen + 1);
      const respPtr = inst.exports.mcp_alloc(respMax);

      const mem = new Uint8Array(inst.exports.memory.buffer);
      mem.set(bodyArr, reqPtr);
      mem[reqPtr + reqLen] = 0;

      const n = inst.exports.mcp_handle_wasm(reqPtr, reqLen, respPtr, respMax);

      const respBody = dec.decode(mem.slice(respPtr, respPtr + n));

      return new Response(respBody, {
        headers: {
          "Content-Type":                "application/json",
          "Access-Control-Allow-Origin": env.CORS_ORIGINS || "https://claude.ai",
          "X-Content-Type-Options":      "nosniff",
        },
      });
    } catch (err) {
      return new Response(
        JSON.stringify({ jsonrpc: "2.0", id: null,
          error: { code: -32603, message: "Internal error: " + err.message } }),
        { status: 500, headers: { "Content-Type": "application/json" } });
    }
  },
};
