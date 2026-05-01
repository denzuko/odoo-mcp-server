# odoo-mcp-server

MCP server for Odoo 15+ XML-RPC. Written in C99 on the BCHS stack.
One source tree, two build targets: native FreeBSD ELF and `wasm32-wasi`
for Cloudflare Workers. The only platform-specific code is a single
`#ifdef __wasm__` in `net.h`.

```
cc -o nob nob.c && ./nob
```

## Tools

| Tool | Odoo method | Description |
|---|---|---|
| `search_read_records` | `search_read` | Domain-filtered field projection, limit/offset/order |
| `get_model_fields` | `fields_get` | Schema introspection |
| `create_record` | `create` | Create record, returns new ID |
| `update_record` | `write` | Update records by ID list |

## Build

Requires: `cc`, `kcgi`, `libtls` (FreeBSD: `pkg install kcgi libressl`)

```sh
# Bootstrap once
cc nob.c -o nob          # native target

# Native ELF
./nob
# → build/odoo-mcp-server

# wasm32-wasi module (requires wasi-sdk)
cc -Dwasm nob.c -o nob  # wasm target && ./nob

# Clean
./nob clean
```

## Run (native) and Server deploy (Podman Quadlet)

```sh
# As root — creates odoo-mcp service account, installs quadlet units,
# generates env file, starts service, smoke-tests /healthz
machinectl shell root@ env \
    ODOO_URL=https://dapla.net \
    ODOO_DB=your_db \
    ODOO_USER=you@dapla.net \
    ODOO_API_KEY=your_key \
./odoo_mcp_setup.sh
```

The container image is built by CI and pushed to
`ghcr.io/denzuko/odoo-mcp-server`. The quadlet unit pulls it
automatically via `AutoUpdate=registry`.

Add the HAProxy stanza from `examples/haproxy-odoo-mcp.cfg` to route
`mcp.dapla.net/*` to the container.

## Cloudflare Workers deploy

```sh
# Build the WASM module
cc -Dwasm nob.c -o nob  # wasm target && ./nob

# Deploy via Terraform (wrangler called via local-exec)
echo init apply | xargs -n1 -I... -- terraform --chdir=terraform/...
```

See `docs/secrets.md` for required GitHub Actions secrets and
Cloudflare API token permissions.

## Claude Desktop config

```json
{
  "mcpServers": {
    "odoo": {
      "command": "/path/to/odoo-mcp-server",
      "env": {
        "ODOO_URL":     "https://dapla.net",
        "ODOO_DB":      "your_db",
        "ODOO_USER":    "you@dapla.net",
        "ODOO_API_KEY": "your_api_key"
      }
    }
  }
}
```

## Policy gates

All four gates must pass before `terraform apply` runs:

```sh
opa eval -d policy/c_quality.rego      -i cppcheck.sarif.json \
  --fail-defined 'data.odoo_mcp.c_quality.violations[_]'

opa eval -d policy/terraform_sast.rego -i kics-tf.sarif.json \
  --fail-defined 'data.odoo_mcp.terraform_sast.violations[_]'

conftest test terraform/plan.json \
  --policy policy/ --namespace odoo_mcp.terraform_plan

opa eval -d policy/sarif.rego          -i osv.sarif.json \
  --fail-defined 'data.odoo_mcp.sarif.violations[_]'
```

## Docs

Full documentation: https://denzuko.github.io/odoo-mcp-server/

Secrets reference: `docs/secrets.md`

## Licence

BSD 2-Clause — see `LICENSE.txt`.

Vendored dependencies (`nob.h`, `arena.h`, `sv.h`) retain their
original MIT licences as documented in `LICENSE.txt`.
