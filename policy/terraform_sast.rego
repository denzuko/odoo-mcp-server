# policy/terraform_sast.rego — Terraform HCL SAST gate
#
# Consumes SARIF 2.1.0 produced by:
#   trivy config --format sarif --output trivy.sarif.json ./terraform/
#   checkov -d terraform/ --output sarif > checkov.sarif.json
#
# Both tools scan HCL statically — no cloud creds needed, no plan required.
# Trivy (successor to tfsec) catches Cloudflare-specific misconfigs.
# Checkov adds compliance policy coverage (CIS, NIST, SOC2 mappings).
#
# Gate: any violation in blocked_checks or above severity_threshold
# causes `opa eval --fail-defined` to exit non-zero, blocking CI.
#
# Run:
#   opa eval -d policy/terraform_sast.rego -i merged_tf.sarif.json \
#     --fail-defined 'data.odoo_mcp.terraform_sast.violations[_]'
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# MIT License

package odoo_mcp.terraform_sast

import future.keywords.if
import future.keywords.in

# ── Severity threshold ──────────────────────────────────────────────── #

# SARIF levels that hard-block CI
blocked_levels := {"error"}

# Checkov / Trivy check IDs that are unconditional hard-blocks
# regardless of severity (security-critical for our CF Workers deployment)
blocked_checks := {
    # Secrets in plaintext — Checkov
    "CKV_SECRET_6",       # hard-coded credentials
    "CKV2_CF_1",          # Cloudflare: WAF not enabled on zone
    "CKV2_CF_2",          # Cloudflare: HTTPS-only not enforced
    "CKV2_CF_3",          # Cloudflare: minimum TLS version < 1.2

    # Trivy / tfsec Cloudflare checks
    "cloudflare-workers-no-plaintext-secrets",
    "cloudflare-zone-enforce-https",
    "cloudflare-zone-minimum-tls-version",

    # General IaC hygiene — always block
    "AVD-GEN-0001",       # sensitive value in output (not marked sensitive)
    "AVD-GEN-0002",       # hardcoded credentials in resource
}

# ── Allowed exceptions ─────────────────────────────────────────────── #
# Checks we explicitly accept for this deployment with documented rationale.
# Format: set of ruleIds that should NOT produce violations.

allowed_exceptions := {
    # worker_bundle.js contains inlined base64 WASM — not a secret.
    # Trivy may flag the large base64 blob as a potential secret.
    "trivy-cloudflare-wasm-base64-false-positive",
}

# ── Violations ─────────────────────────────────────────────────────── #

violations[msg] if {
    run    := input.runs[_]
    result := run.results[_]
    result.level in blocked_levels
    not result.ruleId in allowed_exceptions
    loc    := result.locations[0].physicalLocation
    msg := sprintf("TF SAST error [%v] in %v:%v — %v",
        [result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

violations[msg] if {
    run    := input.runs[_]
    result := run.results[_]
    result.ruleId in blocked_checks
    not result.ruleId in allowed_exceptions
    loc    := result.locations[0].physicalLocation
    msg := sprintf("TF SAST blocked check [%v] in %v:%v — %v",
        [result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

# ── Summary ─────────────────────────────────────────────────────────── #

summary := {
    "pass":            count(violations) == 0,
    "violation_count": count(violations),
}
