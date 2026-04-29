#!/usr/bin/env node
/**
 * scripts/bundle_wasm.js
 *
 * Reads odoo-mcp.wasm, base64-encodes it, and produces
 * workers/worker_bundle.js — a self-contained CF Workers script
 * with the WASM module inlined.
 *
 * Required because cloudflare_worker_version in Terraform provider v5
 * does not yet support separate wasm_module uploads (issue #6852).
 * When that's fixed, delete this script and update main.tf to use
 * separate content_file + modules[].
 *
 * Usage (run after ./nob wasm):
 *   node scripts/bundle_wasm.js
 *
 * Inputs:  odoo-mcp.wasm, workers/worker.js
 * Output:  workers/worker_bundle.js
 */

const fs   = require("fs");
const path = require("path");

const ROOT      = path.resolve(__dirname, "..");
const WASM_PATH = path.join(ROOT, "odoo-mcp.wasm");
const SRC_PATH  = path.join(ROOT, "workers", "worker.js");
const OUT_PATH  = path.join(ROOT, "workers", "worker_bundle.js");

if (!fs.existsSync(WASM_PATH)) {
  console.error(`ERROR: ${WASM_PATH} not found — run ./nob wasm first`);
  process.exit(1);
}

const wasmBytes  = fs.readFileSync(WASM_PATH);
const wasmB64    = wasmBytes.toString("base64");
const workerSrc  = fs.readFileSync(SRC_PATH, "utf8");

// Replace the `import wasm from "./odoo-mcp.wasm"` line with an inline
// WebAssembly.Module compiled from the base64 bytes.
const inlineHeader = `
// ── WASM inlined by scripts/bundle_wasm.js ──────────────────────────────
// Generated at build time — do not edit manually.
const __wasm_b64 = "${wasmB64}";
function __b64decode(b64) {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes.buffer;
}
const wasm = new WebAssembly.Module(__b64decode(__wasm_b64));
// ──────────────────────────────────────────────────────────────────────────
`;

// Remove the import line for wasm and prepend inline header
const bundled = workerSrc
  .replace(/^import wasm from ["']\.\/odoo-mcp\.wasm["'];?\s*\n/m, "")
  .replace(/^/, inlineHeader);

fs.writeFileSync(OUT_PATH, bundled, "utf8");

const kb = (wasmBytes.length / 1024).toFixed(1);
console.log(`bundle_wasm: OK — ${kb} KB WASM inlined → ${OUT_PATH}`);
