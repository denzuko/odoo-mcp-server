output "worker_name" {
  description = "Deployed Worker name"
  value       = cloudflare_workers_script.odoo_mcp.name
}

output "mcp_endpoint" {
  description = "Public MCP endpoint URL"
  value       = "https://${split("/*", var.worker_route)[0]}/mcp"
}

output "worker_dev_url" {
  description = "workers.dev preview URL (always available)"
  value       = "https://${var.worker_name}.${var.cloudflare_account_id}.workers.dev/mcp"
}
