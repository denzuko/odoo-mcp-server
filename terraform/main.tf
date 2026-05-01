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

# ── Route ─────────────────────────────────────────────────────────────── #

resource "cloudflare_workers_route" "odoo_mcp" {
  zone_id     = var.cloudflare_zone_id
  pattern     = var.worker_route
  script_name = cloudflare_workers_script.odoo_mcp.name
}

# ── CycloneDX SBOM ────────────────────────────────────────────────────── #

resource "null_resource" "sbom" {
  triggers = { script_id = cloudflare_workers_script.odoo_mcp.id }

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
