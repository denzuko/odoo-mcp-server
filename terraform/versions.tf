terraform {
  required_version = ">= 1.15.0"

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

  # Remote state — Terraform Cloud or S3 backend
  # Uncomment and configure one of these for production:
  #
  # backend "remote" {
  #   organization = "da-planet-security"
  #   workspaces { name = "odoo-mcp-server" }
  # }
  #
  # backend "s3" {
  #   bucket         = "da-planet-tfstate"
  #   key            = "odoo-mcp-server/terraform.tfstate"
  #   region         = "us-east-1"
  #   encrypt        = true
  # }
}
