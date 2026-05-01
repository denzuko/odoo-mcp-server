# main.tf — Cloudflare Workers deployment for odoo-mcp-server
#
# Verified against registry.terraform.io/providers/cloudflare/cloudflare/4.47+
# Resources used:
#   cloudflare_workers_script — script + secret_text_binding blocks (v4 plural form)
#   cloudflare_worker_route   — route pattern (zone_id, pattern, script_name)
#
# cloudflare_worker_script (singular) is deprecated in v4 — use cloudflare_workers_script.
# cloudflare_worker_secret does not exist in v4 — secrets are secret_text_binding blocks.
#
# Code upload: wrangler local-exec. The cloudflare_workers_script content is a
# placeholder; wrangler overwrites it. Terraform manages secrets and routes.

provider "cloudflare" {
  api_token = var.cloudflare_api_token
}

# ── Worker script + secrets ───────────────────────────────────────────── #

resource "cloudflare_workers_script" "odoo_mcp" {
  account_id = var.cloudflare_account_id
  name       = var.worker_name
  # Minimal valid Worker — wrangler overwrites with actual WASM module.
  # Cloudflare rejects scripts with no event handler (error 10021).
  content    = "addEventListener('fetch',e=>e.respondWith(new Response('initializing',{status:503})))"

  secret_text_binding {
    name = "ODOO_URL"
    text = var.odoo_url
  }

  secret_text_binding {
    name = "ODOO_DB"
    text = var.odoo_db
  }

  secret_text_binding {
    name = "ODOO_USER"
    text = var.odoo_user
  }

  secret_text_binding {
    name = "ODOO_API_KEY"
    text = var.odoo_api_key
  }

  secret_text_binding {
    name = "CORS_ORIGINS"
    text = var.cors_origins
  }
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
    script_name = cloudflare_workers_script.odoo_mcp.name
  }

  provisioner "local-exec" {
    working_dir = "${path.module}/../workers"
    command     = "npx wrangler@latest deploy --name ${cloudflare_workers_script.odoo_mcp.name}"
    environment = {
      CLOUDFLARE_API_TOKEN  = var.cloudflare_api_token
      CLOUDFLARE_ACCOUNT_ID = var.cloudflare_account_id
    }
  }

  depends_on = [cloudflare_workers_script.odoo_mcp, null_resource.wasm_download]
}

# ── Route ─────────────────────────────────────────────────────────────── #

resource "cloudflare_workers_route" "odoo_mcp" {
  zone_id     = var.cloudflare_zone_id
  pattern     = var.worker_route
  script_name = cloudflare_workers_script.odoo_mcp.name
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
          --project-version "${cloudflare_workers_script.odoo_mcp.name}" \
          . && echo "SBOM: terraform.cdx.json written"
      else
        echo "WARNING: cdxgen not found — skipping Terraform SBOM"
      fi
    SH
  }

  depends_on = [cloudflare_workers_route.odoo_mcp]
}
