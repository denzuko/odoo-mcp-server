# main.tf — Cloudflare Workers deployment for odoo-mcp-server
#
# Provider v4 resource names (from registry.terraform.io/providers/cloudflare/cloudflare/4.52):
#   cloudflare_worker_script  — manages the Worker script content + bindings
#   cloudflare_worker_secret  — standalone secret (account_id, script_name, name, secret_text)
#   cloudflare_worker_route   — route pattern (zone_id, pattern, script_name)
#
# Code upload strategy: wrangler via local-exec (provider issue #6852).
# cloudflare_worker_script used as a placeholder resource so Terraform
# tracks the Worker's existence; wrangler handles actual code upload.

provider "cloudflare" {
  api_token = var.cloudflare_api_token
}

# ── Worker script (placeholder — wrangler uploads actual content) ─────── #

resource "cloudflare_worker_script" "odoo_mcp" {
  account_id = var.cloudflare_account_id
  name       = var.worker_name
  content    = "// placeholder — wrangler manages actual content"
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
    command = "curl -sLo ${path.module}/../workers/odoo-mcp-server.wasm '${local.wasm_url}'"
  }
}

resource "null_resource" "wrangler_deploy" {
  triggers = {
    deploy_hash = local.deploy_trigger
    script_name = cloudflare_worker_script.odoo_mcp.name
  }

  provisioner "local-exec" {
    working_dir = "${path.module}/../workers"
    command     = "npx wrangler@latest deploy --name ${cloudflare_worker_script.odoo_mcp.name}"
    environment = {
      CLOUDFLARE_API_TOKEN  = var.cloudflare_api_token
      CLOUDFLARE_ACCOUNT_ID = var.cloudflare_account_id
    }
  }

  depends_on = [cloudflare_worker_script.odoo_mcp, null_resource.wasm_download]
}

# ── Secrets (v4: cloudflare_worker_secret) ────────────────────────────── #

resource "cloudflare_worker_secret" "odoo_url" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker_script.odoo_mcp.name
  name        = "ODOO_URL"
  secret_text = var.odoo_url
}

resource "cloudflare_worker_secret" "odoo_db" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker_script.odoo_mcp.name
  name        = "ODOO_DB"
  secret_text = var.odoo_db
}

resource "cloudflare_worker_secret" "odoo_user" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker_script.odoo_mcp.name
  name        = "ODOO_USER"
  secret_text = var.odoo_user
}

resource "cloudflare_worker_secret" "odoo_api_key" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker_script.odoo_mcp.name
  name        = "ODOO_API_KEY"
  secret_text = var.odoo_api_key
}

resource "cloudflare_worker_secret" "cors_origins" {
  account_id  = var.cloudflare_account_id
  script_name = cloudflare_worker_script.odoo_mcp.name
  name        = "CORS_ORIGINS"
  secret_text = var.cors_origins
}

# ── Route (v4: cloudflare_worker_route) ───────────────────────────────── #

resource "cloudflare_worker_route" "odoo_mcp" {
  zone_id     = var.cloudflare_zone_id
  pattern     = var.worker_route
  script_name = cloudflare_worker_script.odoo_mcp.name
  depends_on  = [null_resource.wrangler_deploy]
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
          --project-version "${cloudflare_worker_script.odoo_mcp.name}" \
          . && echo "SBOM: terraform.cdx.json written"
      else
        echo "WARNING: cdxgen not found — skipping Terraform SBOM"
      fi
    SH
  }

  depends_on = [cloudflare_worker_route.odoo_mcp]
}
