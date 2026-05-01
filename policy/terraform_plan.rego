# policy/terraform_plan.rego — Terraform plan-time policy gate
#
# Consumes the JSON output of `terraform show -json plan.tfplan`.
# Run via conftest or direct OPA eval.
#
# Usage (conftest):
#   terraform plan -out=plan.tfplan
#   terraform show -json plan.tfplan > plan.json
#   conftest test plan.json --policy policy/ --namespace odoo_mcp.terraform_plan
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

package odoo_mcp.terraform_plan

import future.keywords.contains
import future.keywords.if
import future.keywords.in

# ── Rule 1: plaintext secret field outside workers_script resource ────── #

deny contains msg if {
    r := input.resource_changes[_]
    r.change.actions[_] in {"create", "update"}
    r.type != "cloudflare_workers_script"
    _ = r.change.after.secret_text
    msg := sprintf(
        "DENY: plaintext 'secret_text' field in non-script resource %v (%v)",
        [r.address, r.type])
}

# ── Rule 2: workers_script secret_text_binding must be sensitive in plan  #

deny contains msg if {
    r := input.resource_changes[_]
    r.change.actions[_] in {"create", "update"}
    r.type == "cloudflare_workers_script"
    not r.change.after_sensitive.secret_text_binding == true
    msg := sprintf(
        "DENY: cloudflare_workers_script %v — secret_text_binding not marked sensitive",
        [r.address])
}

# ── Rule 3: route patterns must be under mcp.dapla.net ───────────────── #

deny contains msg if {
    r := input.resource_changes[_]
    r.change.actions[_] in {"create", "update"}
    r.type == "cloudflare_workers_route"
    pattern := r.change.after.pattern
    not startswith(pattern, "mcp.dapla.net")
    msg := sprintf(
        "DENY: workers_route pattern '%v' must be under mcp.dapla.net",
        [pattern])
}

# ── Rule 4: compatibility_date must be 2024 or newer ─────────────────── #

deny contains msg if {
    r := input.resource_changes[_]
    r.change.actions[_] in {"create", "update"}
    r.type == "cloudflare_workers_script"
    date := r.change.after.compatibility_date
    date != null
    year := to_number(substring(date, 0, 4))
    year < 2024
    msg := sprintf(
        "DENY: compatibility_date '%v' is older than 2024 — update required",
        [date])
}

# ── Rule 5: destroying script requires approval label ─────────────────── #

deny contains msg if {
    r := input.resource_changes[_]
    r.change.actions[_] == "delete"
    r.type == "cloudflare_workers_script"
    not r.change.before.labels["da_planet_approved_destroy"] == "true"
    msg := sprintf(
        "DENY: destroying %v requires label 'da_planet_approved_destroy=true'",
        [r.address])
}

# ── Rule 6: Worker name must start with odoo-mcp ─────────────────────── #

deny contains msg if {
    r := input.resource_changes[_]
    r.change.actions[_] in {"create", "update"}
    r.type == "cloudflare_workers_script"
    name := r.change.after.name
    not startswith(name, "odoo-mcp")
    msg := sprintf(
        "DENY: cloudflare_workers_script name '%v' must start with 'odoo-mcp'",
        [name])
}

# ── Rule 7: max one route per deployment ──────────────────────────────── #

deny contains msg if {
    routes := [r |
        r := input.resource_changes[_]
        r.change.actions[_] in {"create", "update"}
        r.type == "cloudflare_workers_route"
    ]
    count(routes) > 1
    msg := sprintf(
        "DENY: %v cloudflare_workers_route in plan — maximum 1 allowed",
        [count(routes)])
}

# ── Warnings (non-blocking) ───────────────────────────────────────────── #

warn contains msg if {
    r := input.resource_changes[_]
    r.change.actions[_] in {"create", "update"}
    r.type == "cloudflare_workers_script"
    date := r.change.after.compatibility_date
    date != null
    year := to_number(substring(date, 0, 4))
    year < 2025
    msg := sprintf("WARN: compatibility_date '%v' — consider 2025+", [date])
}

warn contains msg if {
    scripts := [r |
        r := input.resource_changes[_]
        r.change.actions[_] in {"create", "update"}
        r.type == "cloudflare_workers_script"
    ]
    count(scripts) == 0
    msg := "WARN: no cloudflare_workers_script in plan — Odoo creds may be missing"
}

# ── Summary ───────────────────────────────────────────────────────────── #

summary := {
    "pass":       count(deny) == 0,
    "deny_count": count(deny),
    "warn_count": count(warn),
}
