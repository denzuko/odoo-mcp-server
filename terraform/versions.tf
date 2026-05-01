terraform {
  required_version = ">= 1.15.0"

  # Cloudflare provider pinned to v4 — v5 Workers resources (workers_secret,
  # workers_route, worker) are actively being stabilised as of May 2026.
  # Cloudflare themselves advise holding off on v5 migration for Workers.
  # Revisit when cloudflare_workers_script + secret bindings are stable in v5.
  # Upgrade guide: https://registry.terraform.io/providers/cloudflare/cloudflare/latest/docs/guides/version-5-upgrade
  required_providers {
    cloudflare = {
      source  = "cloudflare/cloudflare"
      version = "~> 4.0"
    }
    null = {
      source  = "hashicorp/null"
      version = "~> 3.0"
    }
  }

  cloud {
    organization = "denzuko-devops"
    workspaces {
      name = "odoo-mcp-server"
    }
  }
}
