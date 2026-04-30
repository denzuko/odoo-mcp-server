# main.tf — Cloudflare Workers deployment for odoo-mcp-server
#
# Strategy: cloudflare_worker manages state (name, account, secrets, route).
#            local-exec/wrangler handles code upload until provider issue
#            #6852 is resolved (wasm_module multi-part form upload).
#
# When cloudflare_worker_version gains proper wasm_module support, remove
# the null_resource + local-exec blocks and replace with:
#
#   resource "cloudflare_worker_version" "odoo_mcp" {
#     account_id         = var.cloudflare_account_id
#     worker_id          = cloudflare_worker.odoo_mcp.id
#     compatibility_date = var.compatibility_date
#     main_module        = "worker.js"
#     modules = [{
#       name         = "odoo-mcp.wasm"
#       content_file = "${path.module}/../odoo-mcp.wasm"
#       type         = "application/wasm"
#     }]
#   }
#
# Tracker: https://github.com/cloudflare/terraform-provider-cloudflare/issues/6852

terraform {
  required_version = ">= 1.9.0"
  required_providers {
    cloudflare = {
      source  = "cloudflare/cloudflare"
      version = "~> 5.0"
    }
    null = {
      source  = "hashicorp/null"
      version = "~> 3.0"
    }
  }
}

provider "cloudflare" {
  api_token = var.cloudflare_api_token
}

# ── Worker (name + account — manages existence, not code) ────────────── #

resource "cloudflare_worker" "odoo_mcp" {
  account_id = var.cloudflare_account_id
  name       = var.worker_name

  # net.matrix label schema — mirrors container/OCI label convention
  tags = [
    "net.matrix.organization:Private Ops",
    "net.matrix.orgunit:Matrix NOC",
    "net.matrix.application:odoo-mcp-server",
    "net.matrix.role:mcp-gateway",
    "net.matrix.customer:PVT-01",
    "net.matrix.costcenter:INT-01",
    "net.matrix.environment:production",
    "net.matrix.oid:iso.org.dod.internet.42387",
  ]
}

# ── Code upload via wrangler (local-exec, bridges provider gap) ───────── #
#
# Runs: wrangler deploy --name <worker_name> from the workers/ directory.
# Triggered when either the JS shim or WASM binary changes (tracked by hash).
# wrangler reads CLOUDFLARE_API_TOKEN + CLOUDFLARE_ACCOUNT_ID from env,
# which are injected by the CI pipeline via TF_VAR_ → passed through here.
#
# local-exec runs on the machine executing terraform apply — in CI that is
# the GitHub Actions runner which already has wrangler available via npx.

locals {
  # Hash both artifacts so any change re-triggers the deploy
  worker_js_hash  = filesha256("${path.module}/../workers/worker.js")
  wasm_hash       = fileexists("${path.module}/../odoo-mcp.wasm") ? filesha256("${path.module}/../odoo-mcp.wasm") : "no-wasm"
  deploy_trigger  = sha256("${local.worker_js_hash}${local.wasm_hash}")
}

resource "null_resource" "wrangler_deploy" {
  triggers = {
    deploy_hash = local.deploy_trigger
    worker_name = cloudflare_worker.odoo_mcp.name
  }

  provisioner "local-exec" {
    working_dir = "${path.module}/../workers"
    command     = "npx wrangler@latest deploy --name ${cloudflare_worker.odoo_mcp.name}"
    environment = {
      CLOUDFLARE_API_TOKEN  = var.cloudflare_api_token
      CLOUDFLARE_ACCOUNT_ID = var.cloudflare_account_id
    }
  }

  depends_on = [cloudflare_worker.odoo_mcp]
}

# ── Secrets ───────────────────────────────────────────────────────────── #

resource "cloudflare_workers_secret" "odoo_url" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "ODOO_URL"
  text        = var.odoo_url
  depends_on  = [cloudflare_worker.odoo_mcp]
}

resource "cloudflare_workers_secret" "odoo_db" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "ODOO_DB"
  text        = var.odoo_db
  depends_on  = [cloudflare_worker.odoo_mcp]
}

resource "cloudflare_workers_secret" "odoo_user" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "ODOO_USER"
  text        = var.odoo_user
  depends_on  = [cloudflare_worker.odoo_mcp]
}

resource "cloudflare_workers_secret" "odoo_api_key" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "ODOO_API_KEY"
  text        = var.odoo_api_key
  depends_on  = [cloudflare_worker.odoo_mcp]
}

resource "cloudflare_workers_secret" "cors_origins" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "CORS_ORIGINS"
  text        = var.cors_origins
  depends_on  = [cloudflare_worker.odoo_mcp]
}

# ── Route ─────────────────────────────────────────────────────────────── #

resource "cloudflare_workers_route" "odoo_mcp" {
  zone_id    = var.cloudflare_zone_id
  pattern    = var.worker_route
  script     = cloudflare_worker.odoo_mcp.name
  depends_on = [null_resource.wrangler_deploy]
}
