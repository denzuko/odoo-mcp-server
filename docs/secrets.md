# Secrets & Tokens Reference

All secrets required for CI/CD and deployment. **Never commit real values.**

---

## GitHub Repository Secrets

Set via: **Settings → Secrets and variables → Actions → New repository secret**

### GITHUB_TOKEN (automatic — no setup needed)

GitHub automatically provides `secrets.GITHUB_TOKEN` to every workflow.
Used by: Docker push to GHCR (`packages: write` permission), issue
creation (`jayqi/failed-build-issue-action`), GH Pages deploy.

**No manual secret needed.** Permissions declared in the workflow:

```yaml
permissions:
  contents: read
  packages: write   # for GHCR push (docker job)
  issues:   write   # for notify job (failed-build-issue-action)
  pages:    write   # for pages job
  id-token: write   # for pages OIDC
```

### Cloudflare secrets

| Secret | Description | How to obtain |
|---|---|---|
| `CF_ACCOUNT_ID` | Cloudflare account ID | dash.cloudflare.com → right sidebar under your name |
| `CF_ZONE_ID` | Zone ID for dapla.net | dash.cloudflare.com → dapla.net → Overview → right sidebar |
| `CF_API_TOKEN` | Workers deploy token (production) | See token scopes below |
| `CF_API_TOKEN_PLAN` | Workers read-only token (plan job) | See token scopes below |

**`CF_API_TOKEN` minimum scopes** (create at dash.cloudflare.com → My Profile → API Tokens → Create Token):

```
Account — Workers Scripts      — Edit
Account — Workers KV Storage   — Edit  (if used)
Zone    — Workers Routes       — Edit
Zone    — Zone                 — Read   (to read zone_id)
```

**`CF_API_TOKEN_PLAN` minimum scopes**:

```
Account — Workers Scripts      — Read
Zone    — Workers Routes       — Read
Zone    — Zone                 — Read
```

### Odoo secrets

| Secret | Description |
|---|---|
| `ODOO_URL` | `https://dapla.net` |
| `ODOO_DB` | Your Odoo database name (Settings → Technical → Database Structure) |
| `ODOO_USER` | Login email |
| `ODOO_API_KEY` | Settings → Technical → API Keys → New |

### Terraform Cloud

| Secret | Description | How to obtain |
|---|---|---|
| `TF_API_TOKEN` | Terraform Cloud workspace token | app.terraform.io → your org → Settings → API Tokens → Create team token |

Configure the backend in `terraform/versions.tf`:

```hcl
backend "remote" {
  organization = "da-planet-security"
  workspaces { name = "odoo-mcp-server" }
}
```

Set workspace variables in Terraform Cloud (not in `.tf` files) for
`TF_VAR_cloudflare_api_token`, `TF_VAR_odoo_api_key`, etc. — mark them
**Sensitive**.

---

## GitHub Environments

Two environments in **Settings → Environments**:

**`plan`** — used by `terraform-plan` job (read-only, no approval gate)

Secrets: `CF_API_TOKEN_PLAN`, `CF_ACCOUNT_ID`, `CF_ZONE_ID`,
`ODOO_URL`, `ODOO_DB`, `ODOO_USER`, `ODOO_API_KEY`

**`production`** — used by `terraform-deploy` job

Secrets: `CF_API_TOKEN`, `CF_ACCOUNT_ID`, `CF_ZONE_ID`,
`ODOO_URL`, `ODOO_DB`, `ODOO_USER`, `ODOO_API_KEY`, `TF_API_TOKEN`

**Enable required reviewers** on `production` — at least one human must
approve before `terraform apply` runs.

---

## Server-side env file (native quadlet deployment)

Written by `odoo_mcp_setup.sh` to:
`/home/odoo-mcp/.config/containers/systemd/odoo-mcp.env`

Permissions: `0600`, owned by `odoo-mcp` service account.

```sh
# Non-interactive install:
env ODOO_URL=https://dapla.net \
    ODOO_DB=your_db \
    ODOO_USER=denzuko@dapla.net \
    ODOO_API_KEY=your_api_key \
    sudo ./odoo_mcp_setup.sh
```

**Key rotation:**
```sh
# Edit the env file, then restart:
vi /home/odoo-mcp/.config/containers/systemd/odoo-mcp.env
su -s /bin/sh -c \
  "XDG_RUNTIME_DIR=/run/user/$(id -u odoo-mcp) \
   systemctl --user restart odoo-mcp.service" odoo-mcp
```

---

## Cloudflare Workers secrets (Terraform-managed)

The five secrets deployed by `terraform/main.tf` as `cloudflare_workers_secret`
resources. Injected at `terraform apply` from GitHub `production` environment
secrets → `TF_VAR_*` → Terraform variables. Encrypted at rest by Cloudflare,
never appear in `terraform.tfstate` in plaintext (`sensitive = true`).

| Worker env var | Terraform variable | GitHub secret |
|---|---|---|
| `ODOO_URL` | `TF_VAR_odoo_url` | `ODOO_URL` |
| `ODOO_DB` | `TF_VAR_odoo_db` | `ODOO_DB` |
| `ODOO_USER` | `TF_VAR_odoo_user` | `ODOO_USER` |
| `ODOO_API_KEY` | `TF_VAR_odoo_api_key` | `ODOO_API_KEY` |
| `CORS_ORIGINS` | `TF_VAR_cors_origins` | *(hardcoded default: `https://claude.ai`)* |

---

## What is never committed

- `env/odoo-mcp.env` with real values
- `terraform/*.tfstate`, `terraform/plan.json`, `terraform/plan.tfplan`
- `workers/.dev.vars`
- Any file containing a real `ODOO_API_KEY` or `CF_API_TOKEN`

### Infracost (optional — cost estimation)

| Secret | Description | How to obtain |
|---|---|---|
| `INFRACOST_API_KEY` | Infracost cloud API key | app.infracost.io → Org settings → API keys |

If `INFRACOST_API_KEY` is not set, the infracost step emits a zero-cost stub
and the gate passes. Cost estimation is advisory; set the key to get real
Cloudflare Workers pricing data.
