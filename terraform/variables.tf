variable "cloudflare_api_token" {
  description = "Cloudflare API token with Workers:Edit + Zone:Read permissions"
  type        = string
  sensitive   = true
}

variable "cloudflare_account_id" {
  description = "Cloudflare account ID"
  type        = string
}

variable "cloudflare_zone_id" {
  description = "Zone ID for dapla.net"
  type        = string
}

variable "worker_name" {
  description = "Name of the Cloudflare Worker"
  type        = string
  default     = "odoo-mcp-server"
}

variable "worker_route" {
  description = "Route pattern for the MCP Worker, e.g. mcp.dapla.net/*"
  type        = string
  default     = "mcp.dapla.net/*"
}

variable "compatibility_date" {
  description = "Workers compatibility date"
  type        = string
  default     = "2025-03-01"
}

# ── Odoo secrets — injected as Worker bindings, never in .tf files ────── #

variable "odoo_url" {
  description = "ODOO_URL secret value"
  type        = string
  sensitive   = true
}

variable "odoo_db" {
  description = "ODOO_DB secret value"
  type        = string
  sensitive   = true
}

variable "odoo_user" {
  description = "ODOO_USER secret value"
  type        = string
  sensitive   = true
}

variable "odoo_api_key" {
  description = "ODOO_API_KEY secret value"
  type        = string
  sensitive   = true
}

variable "cors_origins" {
  description = "Comma-separated allowed CORS origins"
  type        = string
  default     = "https://claude.ai"
}
