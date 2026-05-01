# main.tf — Cloudflare Workers deployment for odoo-mcp-server
#
# Provider v5 resource names (v5.5+ — cloudflare_workers_* removed):
#   cloudflare_worker         — Worker script existence
#   cloudflare_worker_secret  — encrypted secrets (was cloudflare_workers_secret)
#   cloudflare_worker_route   — route pattern   (was cloudflare_workers_route)
#
# Code upload via wrangler local-exec until provider issue #6852 is resolved.
#
# Tracker: https://github.com/cloudflare/terraform-provider-cloudflare/issues/6852

provider "cloudflare" {
  api_token = var.cloudflare_api_token
}

# ── Worker ────────────────────────────────────────────────────────────── #

resource "cloudflare_worker" "odoo_mcp" {
  account_id = var.cloudflare_account_id
  name       = var.worker_name
}

# ── Wrangler deploy via local-exec ────────────────────────────────────── #

locals {
  wasm_url       = "https://github.com/${var.github_repo}/releases/download/${var.release_tag}/odoo-mcp-server.wasm"
  worker_js_hash = filesha256("${path.module}/../workers/worker.js")
  deploy_trigger = sha256("${local.worker_js_hash}${var.release_tag}")
}

resource "null_resource" "wasm_download" {
  triggers = { release_tag = var.release_tag }

  provisioner "local-exec" {
    command = <<-SH
      curl -sLo ${path.module}/../workers/odoo-mcp-server.wasm \
        "${local.wasm_url}"
    SH
  }
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

  depends_on = [cloudflare_worker.odoo_mcp, null_resource.wasm_download]
}

# ── Secrets (cloudflare_worker_secret — v5 name) ──────────────────────── #

resource "cloudflare_worker_secret" "odoo_url" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "ODOO_URL"
  secret_text = var.odoo_url
  depends_on  = [cloudflare_worker.odoo_mcp]
}

resource "cloudflare_worker_secret" "odoo_db" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "ODOO_DB"
  secret_text = var.odoo_db
  depends_on  = [cloudflare_worker.odoo_mcp]
}

resource "cloudflare_worker_secret" "odoo_user" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "ODOO_USER"
  secret_text = var.odoo_user
  depends_on  = [cloudflare_worker.odoo_mcp]
}

resource "cloudflare_worker_secret" "odoo_api_key" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "ODOO_API_KEY"
  secret_text = var.odoo_api_key
  depends_on  = [cloudflare_worker.odoo_mcp]
}

resource "cloudflare_worker_secret" "cors_origins" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker.odoo_mcp.name
  name        = "CORS_ORIGINS"
  secret_text = var.cors_origins
  depends_on  = [cloudflare_worker.odoo_mcp]
}

# ── Route (cloudflare_worker_route — v5 name) ─────────────────────────── #

resource "cloudflare_worker_route" "odoo_mcp" {
  zone_id    = var.cloudflare_zone_id
  pattern    = var.worker_route
  script     = cloudflare_worker.odoo_mcp.name
  depends_on = [null_resource.wrangler_deploy]
}

# ── CycloneDX SBOM ────────────────────────────────────────────────────── #

resource "null_resource" "sbom" {
  triggers = { worker_deploy = null_resource.wrangler_deploy.id }

  provisioner "local-exec" {
    working_dir = path.module
    command     = <<-SH
      if command -v cdxgen >/dev/null 2>&1; then
        cdxgen -t terraform \
          -o terraform.cdx.json \
          --spec-version 1.6 \
          --project-name odoo-mcp-server-tf \
          --project-version "${cloudflare_worker.odoo_mcp.name}" \
          . && echo "SBOM: terraform.cdx.json written"
      else
        echo "WARNING: cdxgen not found — skipping Terraform SBOM"
      fi
    SH
  }

  depends_on = [cloudflare_worker_route.odoo_mcp]
}
