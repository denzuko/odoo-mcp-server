# CLAUDE.md — project context for AI assistants

## What this is

`odoo-mcp-server` is a C99/BCHS MCP server bridging any MCP-capable
LLM to an Odoo 15+ instance via XML-RPC. One source tree, two build
targets (native FreeBSD ELF + wasm32-wasi for Cloudflare Workers).

## Stack

- **Language**: C99 (`-std=c99`)
- **HTTP layer**: kcgi (native target only)
- **TLS**: libtls (native target only)
- **Build**: tsoding/nob.h — `cc nob.c -o nob` (native), `cc -Dwasm nob.c -o nob` (WASM)
- **JSON parse**: rxi/sj.h (zero-alloc cursor, public domain)
- **JSON build**: json.h JsonBuf (arena-backed builder, original)
- **Arena**: tsoding/arena.h — `Arena a = {0}`, Region linked-list, auto-grows
- **RC**: tsoding/rc.h — `rc_alloc()`, `rc_acquire()`, `rc_release()`
- **Deploy**: Terraform + `local-exec wrangler` for CF Workers
- **Policy**: OPA/Rego — `opa eval --fail-defined violations[_]`
- **SBOM**: cdxgen → CycloneDX + SPDX; osv-scanner CVE → SARIF
- **SAST**: cppcheck (C), KICS (Terraform HCL)

## Arena pattern (tsoding/arena.h)

No `arena_new()`. No fixed capacity. Zero-init and let regions grow:

```c
Arena root = {0};                    /* long-lived, never rewound */
Arena a    = {0};                    /* request-scoped */
Arena_Mark m = arena_snapshot(&a);   /* save before request work */
/* ... work ... */
arena_rewind(&a, m);                 /* rewind, not reset */
arena_free(&a); arena_free(&root);   /* shutdown only */
```

No scratch arenas. `ARENA_IMPLEMENTATION` defined once in `main.c`.

## RC pattern (tsoding/rc.h)

For objects shared across request boundaries (e.g. registry strings):

```c
MyThing *t = rc_alloc(sizeof *t, my_destroy); /* count starts at 0 */
rc_acquire(t);   /* share */
rc_release(t);   /* release — frees at count 0 */
```

`RC_IMPLEMENTATION` defined once in `main.c`.

## Static data pattern

No `static const char[]` JSON strings. All static data is:
- enum for identity (`McpToolId`)
- struct for shape (`McpTool`, `McpToolRegistry`)
- stored on root arena via `mcp_registry_init(&reg, &root)`

## net.matrix label schema

All containers, OCI images, quadlet units, and Terraform resources carry:

```
net.matrix.organization  Private Ops
net.matrix.orgunit       Matrix NOC
net.matrix.commonname    <service-name>
net.matrix.environment   production | staging | nonprod
net.matrix.application   <app-name>
net.matrix.role          <role>
net.matrix.owner         FC13F74B@matrix.net
net.matrix.customer      PVT-01
net.matrix.costcenter    INT-01
net.matrix.oid           iso.org.dod.internet.42387
net.matrix.duns          iso.org.duns.039271257
```

## Code conventions

- Yoda conditions: `if (NULL == p)`, `if (0 == strcmp(...))`
- All allocation through arena — no `malloc` in hot path
- No `system()`, `popen()`, `exec*()` — Rego AST gate enforces
- Section headers: `/* ── SECTION ── */`
- C99 only — no C11/C2x extensions

## Policy gates (must all pass before deploy)

```sh
opa eval -d policy/c_quality.rego    -i cppcheck.sarif.json \
  --fail-defined 'data.odoo_mcp.c_quality.violations[_]'

opa eval -d policy/terraform_sast.rego -i kics-tf.sarif.json \
  --fail-defined 'data.odoo_mcp.terraform_sast.violations[_]'

conftest test terraform/plan.json \
  --policy policy/ --namespace odoo_mcp.terraform_plan

opa eval -d policy/sarif.rego -i osv.sarif.json \
  --fail-defined 'data.odoo_mcp.sarif.violations[_]'
```

## Workflow (BDD-first)

Order is mandatory. Tests and policy drive code, never the reverse.

1. Open a GitHub Issue
2. Branch: `feat/N-description` or `fix/N-description`
3. Write or update Rego gate(s) that define the policy constraint
4. Write or update xUnit tests in `tests.c` that define the behaviour
5. Write or fix code until `./nob test` passes and all Rego gates pass
6. Update `CHANGELOG.md` under `[Unreleased]`
7. Push → PR → review → squash merge
8. Semver tag on main

If tests or policy do not provide clear direction for a change, stop
and seek clarification before writing code.

## Versioning (semver: major.minor.patch)

```
MAJOR — public API/interface break
        mcp_handle() signature change that breaks callers
        OdooCtx, McpToolRegistry, McpTool struct layout change
        MCP wire protocol version bump that drops compatibility
        Any header change that requires callers to recompile differently

MINOR — new non-breaking capability (v1.1.0, v1.2.0, ...)
        New MCP tool added to registry
        New Rego policy gate added
        New test suite file
        New deploy target or CI job

PATCH — everything else (v1.1.1, v1.1.2, ...; patch easily exceeds 100)
        Bug fix in existing code
        Documentation update
        Build system change (nob.c, config.h)
        Label schema, file layout, naming corrections
        CI/CD pipeline fix
        Dependency vendor update (no API change)
        Test added to existing suite
        Policy rule tightened within existing gate
```

**Examples from this project's history:**
- `v1.0.0` → `v1.0.1`: CF Workers deploy added (patch — no API change)
- `v1.0.9` → `v1.1.0`: xUnit test suite added (minor — new capability)
- Future `v2.0.0`: only if `mcp_handle()` or wire protocol breaks

**Never** bump major for internal restructuring, build changes,
documentation, or adding new non-breaking features.
**Never** treat minor as a release counter — it tracks capability additions.
**Patch can and should exceed 100** across the life of a minor version.

## What NOT to do

- Do not add Python, Go, or any interpreted language to this repo
- Do not add `#ifdef __wasm__` outside `net.h`
- Do not call `system()` or `popen()` — the Rego gate will block it
- Do not vendor new deps without updating `LICENSE.txt`
- Do not add Trivy or Checkov to CI (supply chain / Cortex Cloud)
- Do not commit `sbom.cdx.json`, `sbom.spdx.json`, or `*.sarif.json`
  (regenerated by CI, listed in `.gitignore`)
- Do not remove the nob binary in do_clean — nob rebuilds itself if nob.c changes
- Do not allow code, policy, BDD tests, and documentation to drift
- Do not use const string arrays when a struct, enum, lookup table, and factory will do
- Do not bump major version for anything other than a public interface break
- Do not bump minor version for patches — patch counter is for fixes
- Do not write code before writing the test and policy that defines it
