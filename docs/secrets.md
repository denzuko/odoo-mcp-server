# Secrets & Tokens Reference

All secrets for CI/CD and server deployment. **Never commit real values.**

---

## GitHub Repository Secrets

Set via: **Settings → Secrets and variables → Actions → New repository secret**

### Cloudflare (CI deploy)

| Secret | Description | How to obtain |
|---|---|---|
| `CF_ACCOUNT_ID` | Cloudflare account ID | dash.cloudflare.com → right sidebar |
| `CF_ZONE_ID` | Zone ID for dapla.net | dash.cloudflare.com → dapla.net → Overview → right sidebar |
| `CF_API_TOKEN` | Workers:Edit token (production deploy) | dash.cloudflare.com → My Profile → API Tokens → Create Token → "Edit Cloudflare Workers" template |
| `CF_API_TOKEN_PLAN` | Workers:Read token (plan-only, no deploy) | Same as above but with Read-only permissions |

**Minimum token permissions for `CF_API_TOKEN`:**
- Zone — Workers Routes — Edit
- Account — Workers Scripts — Edit
- Account — Workers KV Storage — Edit (if used)

**Minimum token permissions for `CF_API_TOKEN_PLAN`:**
- Zone — Workers Routes — Read
- Account — Workers Scripts — Read

### Odoo credentials (CI integration test + Terraform secrets)

| Secret | Description |
|---|---|
| `ODOO_URL` | `https://dapla.net` |
| `ODOO_DB` | Your Odoo database name |
| `ODOO_USER` | Login email (e.g. `denzuko@dapla.net`) |
| `ODOO_API_KEY` | Odoo user API key — Settings → Technical → API Keys → New |

### GitHub Environments

Two environments must exist in **Settings → Environments**:

**`plan`** — used by `terraform-plan` job
- Secrets: `CF_API_TOKEN_PLAN`, `CF_ACCOUNT_ID`, `CF_ZONE_ID`, all `ODOO_*`
- No required reviewers (plan is read-only)

**`production`** — used by `terraform-deploy` job
- Secrets: `CF_API_TOKEN`, `CF_ACCOUNT_ID`, `CF_ZONE_ID`, all `ODOO_*`
- **Enable required reviewers** — at least one human must approve before apply

---

## Server-Side Secrets (native quadlet deployment)

Managed by `odoo_mcp_setup.sh`. Written to:

```
/home/odoo-mcp/.config/containers/systemd/odoo-mcp.env
```

Permissions: `0600`, owned by `odoo-mcp` service account. Never world-readable.

The setup script accepts credentials via environment variables or interactive
prompt. To inject non-interactively:

```sh
ODOO_URL=https://dapla.net \
ODOO_DB=your_db \
ODOO_USER=denzuko@dapla.net \
ODOO_API_KEY=your_api_key \
  ./odoo_mcp_setup.sh
```

To rotate the API key after initial install:

```sh
# Edit the env file directly as root
vi /home/odoo-mcp/.config/containers/systemd/odoo-mcp.env

# Restart the container to pick up the new key
su -s /bin/sh -c \
  "XDG_RUNTIME_DIR=/run/user/$(id -u odoo-mcp) \
   systemctl --user restart odoo-mcp.service" odoo-mcp
```

---

## Cloudflare Workers Secrets (WASM deploy via Terraform)

Managed by `cloudflare_workers_secret` resources in `terraform/main.tf`.
Injected at `terraform apply` time from `TF_VAR_*` environment variables,
which are sourced from the GitHub `production` environment secrets.

The five secrets deployed to the Worker:

| Wrangler name | Terraform variable | Value |
|---|---|---|
| `ODOO_URL` | `TF_VAR_odoo_url` | `https://dapla.net` |
| `ODOO_DB` | `TF_VAR_odoo_db` | your database name |
| `ODOO_USER` | `TF_VAR_odoo_user` | login email |
| `ODOO_API_KEY` | `TF_VAR_odoo_api_key` | Odoo API key |
| `CORS_ORIGINS` | `TF_VAR_cors_origins` | `https://claude.ai` |

Secrets are **encrypted at rest** by Cloudflare and never appear in
`terraform.tfstate` in plaintext (marked `sensitive = true`).

---

## What is never committed

- `env/odoo-mcp.env` with real values — template only, real copy lives on server
- `terraform/*.tfstate` — listed in `.gitignore`
- `terraform/plan.json` — listed in `.gitignore`
- `workers/.dev.vars` — listed in `.gitignore`
- Any file containing `ODOO_API_KEY` with a real value
