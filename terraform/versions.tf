terraform {
  required_version = ">= 1.15.0"

  cloud {
    organization = "denzuko-devops"
    workspaces {
      name = "odoo-mcp-server"
    }
  }

  required_providers {
    cloudflare = {
      source  = "cloudflare/cloudflare"
      version = "~> 5.19"
    }
    null = {
      source  = "hashicorp/null"
      version = "~> 3.0"
    }
  }
}
