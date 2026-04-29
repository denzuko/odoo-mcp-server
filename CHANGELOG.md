# Changelog

All notable changes to odoo-mcp-server are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

---

## [1.2.0] — 2026-04-29

### Changed

- `terraform/main.tf` — replaced `cloudflare_worker_version` +
  `scripts/bundle_wasm.js` workaround with `cloudflare_worker` +
  `null_resource`/`local-exec wrangler deploy`. Triggered by
  sha256 of `worker.js` + `odoo-mcp.wasm`. Migration comment
  documents `modules[]` path for when provider issue #6852 is fixed.
- `policy/terraform_sast.rego` — replaced Trivy (supply chain attack,
  Aqua Security, March 2026) and Checkov (Cortex Cloud/Palo Alto
  Networks) with KICS (Checkmarx, Apache 2.0, native Rego).
- CI `sbom` job — cdxgen produces CycloneDX JSON + SPDX JSON;
  osv-scanner ingests CycloneDX SBOM for CVE → SARIF.

### Removed

- `scripts/bundle_wasm.js` — no longer needed.
- Trivy GitHub Action — supply chain compromise.
- Checkov GitHub Action — absorbed into commercial Cortex Cloud.

---

## [1.1.0] — 2026-04-29

### Added

- `terraform/` — Cloudflare Workers IaC deployment via provider v5.
  `cloudflare_worker`, `cloudflare_workers_secret` ×5,
  `cloudflare_workers_route`.
- `policy/terraform_sast.rego` — OPA gate over Trivy + Checkov SARIF
  (superseded in 1.2.0 by KICS).
- `policy/terraform_plan.rego` — OPA/conftest gate over
  `terraform show -json`. Seven deny rules covering blast radius,
  sensitive fields, compat date, naming, secret destroy approval.
- `scripts/bundle_wasm.js` — CI step inlining WASM into JS bundle
  (workaround for provider issue #6852; removed in 1.2.0).

---

## [1.0.0] — 2026-04-29

### Added

- `main.c` — kcgi HTTP server, `/mcp` (MCP JSON-RPC 2.0) +
  `/healthz`. FastCGI + plain CGI modes. Native target only.
- `mcp.c` / `mcp.h` — MCP protocol: `initialize`, `tools/list`,
  `tools/call` dispatch for all four Odoo tools.
- `odoo.c` / `odoo.h` — Odoo XML-RPC client: `authenticate` +
  `execute_kw`. Hand-rolled XML-RPC envelope builder, response
  parser, and JSON→XML-RPC converter.
- `net.c` / `net.h` — Transport abstraction. The single `#ifdef`
  boundary: BSD sockets + libtls (native) vs. `http_fetch` import
  from CF Worker JS shim (`__wasm__`).
- `json.h` — Arena-backed JSON builder + recursive-descent parser.
- `arena.h` — Bump-pointer arena allocator (tsoding pattern).
- `sv.h` — String view, zero-copy (tsoding pattern).
- `config.h` — `ODOO_*` env var binding, fail-fast on missing vars.
- `nob.c` — Build driver. `./nob` → native ELF, `./nob wasm` →
  `wasm32-wasi` module, `./nob clean`.
- `nob.h` — Vendored tsoding/nob.h v3.8.2.
- `workers/worker.js` — 15-line CF Workers JS shim. Loads
  `odoo-mcp.wasm`, bridges `http_fetch` import to Workers `fetch()`.
- `policy/c_quality.rego` — OPA gate over cppcheck + clang-tidy
  SARIF. Blocks: null-deref, buffer OOB, UAF, uninit reads,
  format-string injection, banned APIs, style violations.
- `policy/sarif.rego` — OPA CVE gate over osv-scanner SARIF.
  Blocks CVSS ≥ 7.0.
- `docs/index.html` — GitHub Pages site. Da Planet design system
  (Share Tech Mono / Rajdhani, canonical tokens from
  thelounge-theme-daplanet v1.0.8).
