# policy/terraform_sast.rego — Terraform HCL SAST gate
#
# Input: SARIF 2.1.0 produced by KICS (Checkmarx/Open Source):
#   kics scan -p terraform/ --report-formats sarif \
#             --output-path . --output-name kics-tf
#
# KICS uses native Rego queries internally and emits SARIF with
# severity in result.properties.severity (HIGH/MEDIUM/LOW/INFO)
# and CWE/compliance mappings in result.properties.
#
# Replaces Trivy (supply chain compromised, Aqua Security, March 2026)
# and Checkov (absorbed into Cortex Cloud / Palo Alto Networks).
# KICS is Checkmarx open-source, Apache 2.0, actively maintained,
# no commercial lock-in, native Rego policy engine.
#
# Gate: --fail-defined 'data.odoo_mcp.terraform_sast.violations[_]'
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

package odoo_mcp.terraform_sast

import future.keywords.contains
import future.keywords.if
import future.keywords.in

# ── Severity threshold ──────────────────────────────────────────────── #

# KICS maps severity into result.level (SARIF) and result.properties.severity
blocked_levels     := {"error"}
blocked_severities := {"HIGH", "CRITICAL"}

# ── KICS query IDs that are unconditional blocks ─────────────────────── #
# Full KICS query library: https://docs.kics.io/latest/queries/
# Cloudflare-specific + general secrets/IaC hygiene

blocked_queries := {
    # Cloudflare — WAF / TLS / HTTPS
    "cf-workers-plaintext-secret",     # secret in non-sensitive attribute
    "cloudflare-zone-waf-disabled",    # WAF not enabled on zone
    "cloudflare-zone-https-only",      # HTTPS-only not set
    "cloudflare-zone-min-tls-version", # TLS < 1.2

    # Secrets hygiene — KICS universal checks
    "a9dfec39-a740-4105-bbd6-721ba163c053",  # passwords in variables
    "e8c80448-31d8-4755-85fc-6dbab69c2717",  # sensitive output not marked sensitive
    "d5e83b32-56dd-4247-b12e-9b4932234bf7",  # hardcoded credentials

    # Terraform state hygiene
    "d1646197-4d22-4de1-b84b-0e1f3e54fe4c",  # backend not configured
    "1e434b25-8763-4b00-a5ca-ca03b7abbb66",  # no required_providers pinning

    # local-exec security — our wrangler workaround must not leak tokens
    "local-exec-sensitive-env",  # custom: sensitive vars in local-exec env
}

# ── Allowed exceptions ──────────────────────────────────────────────── #
# Documented false positives for this specific deployment.

allowed_exceptions := {
    # Backend not configured: intentional for now (using local state in dev,
    # GitHub Actions uses TF_BACKEND env — not a .tf file misconfiguration).
    "d1646197-4d22-4de1-b84b-0e1f3e54fe4c",
}

# ── Violations ──────────────────────────────────────────────────────── #

violations contains msg if {
    run    := input.runs[_]
    result := run.results[_]
    result.level in blocked_levels
    not result.ruleId in allowed_exceptions
    loc    := result.locations[0].physicalLocation
    msg := sprintf("TF SAST [error] %v in %v:%v — %v",
        [result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

violations contains msg if {
    run    := input.runs[_]
    result := run.results[_]
    sev    := object.get(result.properties, "severity", "")
    sev    in blocked_severities
    not result.ruleId in allowed_exceptions
    loc    := result.locations[0].physicalLocation
    msg := sprintf("TF SAST [%v] %v in %v:%v — %v",
        [sev,
         result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

violations contains msg if {
    run    := input.runs[_]
    result := run.results[_]
    result.ruleId in blocked_queries
    not result.ruleId in allowed_exceptions
    loc    := result.locations[0].physicalLocation
    msg := sprintf("TF SAST blocked query [%v] in %v:%v — %v",
        [result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

# ── Summary ─────────────────────────────────────────────────────────── #

summary := {
    "pass":            count(violations) == 0,
    "violation_count": count(violations),
    "tool":            "kics",
}
