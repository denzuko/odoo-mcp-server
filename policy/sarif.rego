# policy/sarif.rego — CVE severity gate
#
# Consumes SARIF 2.1.0 produced by osv-scanner --format=sarif.
# Blocks on any finding with CVSS score >= 7.0 (HIGH or CRITICAL).
#
# Usage:
#   opa eval -d policy/sarif.rego -i osv.sarif.json \
#     --fail-defined 'data.odoo_mcp.sarif.violations[_]'
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

package odoo_mcp.sarif

import future.keywords.contains
import future.keywords.if
import future.keywords.in

# CVSS score threshold — findings at or above this level block the gate
cvss_threshold := 7.0

# ── Extract CVSS score from a SARIF result ────────────────────────────── #

# osv-scanner embeds CVSS in result.properties.cvssScore (float)
# or in result.properties.severity as "CRITICAL"/"HIGH"/"MEDIUM"/"LOW"

severity_score := {"CRITICAL": 9.0, "HIGH": 7.0, "MEDIUM": 4.0, "LOW": 0.1}

result_score(result) := score if {
    score := result.properties.cvssScore
    is_number(score)
} else := score if {
    sev := upper(result.properties.severity)
    score := severity_score[sev]
} else := 0.0

# ── Violations ────────────────────────────────────────────────────────── #

violations contains msg if {
    run    := input.runs[_]
    result := run.results[_]
    score  := result_score(result)
    score >= cvss_threshold
    rule_id := result.ruleId
    uri     := result.locations[_].physicalLocation.artifactLocation.uri
    msg := sprintf(
        "CVE %v (CVSS %.1f >= %.1f) in %v",
        [rule_id, score, cvss_threshold, uri])
}

# ── Summary ───────────────────────────────────────────────────────────── #

summary := {
    "pass":            count(violations) == 0,
    "violation_count": count(violations),
    "threshold":       cvss_threshold,
}
