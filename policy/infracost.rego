# policy/infracost.rego — Infracost cost threshold gate
#
# Consumes infracost breakdown --format json output.
# Input schema (infracost v0.10+):
#   { "projects": [{
#       "breakdown": {
#         "totalMonthlyCost": "12.34",
#         "resources": [{
#           "address": "...", "type": "...",
#           "monthlyCost": "1.23"
#         }]
#       }
#     }]
#   }
#
# Usage:
#   infracost breakdown --path terraform/ --format json \
#     --out-file infracost.json
#   opa eval -d policy/infracost.rego -i infracost.json \
#     --fail-defined 'data.odoo_mcp.infracost.violations[_]'
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

package odoo_mcp.infracost

import future.keywords.contains
import future.keywords.if
import future.keywords.in

# ── Thresholds ────────────────────────────────────────────────────────── #

# Hard block: total monthly cost across all projects (USD)
monthly_cost_limit := 50.00

# Hard block: single resource monthly cost (USD)
resource_cost_limit := 25.00

# ── Helpers ───────────────────────────────────────────────────────────── #

to_float(s) := f if {
    is_string(s)
    f := to_number(s)
} else := s if {
    is_number(s)
} else := 0.0

# ── Violations ────────────────────────────────────────────────────────── #

# Total cost across all projects exceeds monthly limit
violations contains msg if {
    total := sum([to_float(p.breakdown.totalMonthlyCost) |
                  p := input.projects[_]])
    total > monthly_cost_limit
    msg := sprintf(
        "COST: total monthly cost $%.2f exceeds limit $%.2f",
        [total, monthly_cost_limit])
}

# Single resource exceeds per-resource limit
violations contains msg if {
    project  := input.projects[_]
    resource := project.breakdown.resources[_]
    cost     := to_float(resource.monthlyCost)
    cost > resource_cost_limit
    msg := sprintf(
        "COST: resource %v ($%.2f/mo) exceeds per-resource limit $%.2f",
        [resource.address, cost, resource_cost_limit])
}

# ── Summary ───────────────────────────────────────────────────────────── #

total_monthly_cost := sum([to_float(p.breakdown.totalMonthlyCost) |
                           p := input.projects[_]])

summary := {
    "pass":            count(violations) == 0,
    "violation_count": count(violations),
    "total_monthly_cost_usd": total_monthly_cost,
    "monthly_limit_usd": monthly_cost_limit,
}
