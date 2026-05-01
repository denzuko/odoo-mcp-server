# policy/tfsec.rego — tfsec SARIF gate
#
# Consumes SARIF 2.1.0 produced by tfsec:
#   tfsec terraform/ --format sarif --out tfsec.sarif.json
#
# tfsec uses standard SARIF levels (error/warning/note) and embeds
# severity in result.properties.severity (CRITICAL/HIGH/MEDIUM/LOW).
# Rule IDs are tfsec check IDs: e.g. "general-secrets-no-plaintext-exposure"
#
# Usage:
#   opa eval -d policy/tfsec.rego -i tfsec.sarif.json \
#     --fail-defined 'data.odoo_mcp.tfsec.violations[_]'
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

package odoo_mcp.tfsec

import future.keywords.contains
import future.keywords.if
import future.keywords.in

# ── Severity thresholds ───────────────────────────────────────────────── #

blocked_levels     := {"error"}
blocked_severities := {"CRITICAL", "HIGH"}

# ── tfsec rule IDs that are unconditional blocks ──────────────────────── #
# https://aquasecurity.github.io/tfsec/latest/checks/

blocked_checks := {
    # Secrets in plaintext
    "general-secrets-no-plaintext-exposure",
    "general-secrets-sensitive-in-variable",
    "general-secrets-sensitive-in-local",
    "general-secrets-sensitive-in-attribute",

    # Cloudflare Workers specific
    "cloudflare-workers-no-plain-text-secrets",

    # Sensitive outputs not marked sensitive
    "general-terraform-no-sensitive-output",
}

# ── Allowed exceptions ────────────────────────────────────────────────── #
# Document intentional suppressions here.

allowed_exceptions := {
    # wrangler.toml vars are non-secret net.matrix metadata only.
    # Actual secrets are managed via secret_text_binding, not vars.
}

# ── Violations ────────────────────────────────────────────────────────── #

violations contains msg if {
    run    := input.runs[_]
    result := run.results[_]
    result.level in blocked_levels
    not result.ruleId in allowed_exceptions
    loc    := result.locations[0].physicalLocation
    msg := sprintf("tfsec [error] %v in %v:%v — %v",
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
    msg := sprintf("tfsec [%v] %v in %v:%v — %v",
        [sev,
         result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

violations contains msg if {
    run    := input.runs[_]
    result := run.results[_]
    result.ruleId in blocked_checks
    not result.ruleId in allowed_exceptions
    loc    := result.locations[0].physicalLocation
    msg := sprintf("tfsec blocked check [%v] in %v:%v — %v",
        [result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

# ── Summary ───────────────────────────────────────────────────────────── #

summary := {
    "pass":            count(violations) == 0,
    "violation_count": count(violations),
    "tool":            "tfsec",
}
