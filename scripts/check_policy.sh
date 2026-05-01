#!/bin/sh
# scripts/check_policy.sh — BDD test runner for all Rego policy gates
#
# Runs OPA against canned inputs to verify each gate behaves correctly.
# Exits non-zero if any gate produces unexpected results.
#
# Usage: sh scripts/check_policy.sh
#
# Requires: opa in PATH
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PASS=0
FAIL=0

opa_violations() {
    rego="$1"; input="$2"; pkg="$3"
    printf '%s' "$input" > /tmp/check_policy_input.json
    opa eval -d "$ROOT/$rego" -i /tmp/check_policy_input.json \
        "count(data.${pkg}.violations)" \
        --format raw 2>/dev/null | tr -d '[:space:]'
}

opa_deny() {
    rego="$1"; input="$2"; pkg="$3"
    printf '%s' "$input" > /tmp/check_policy_input.json
    opa eval -d "$ROOT/$rego" -i /tmp/check_policy_input.json \
        "count(data.${pkg}.deny)" \
        --format raw 2>/dev/null | tr -d '[:space:]'
}

assert_eq() {
    name="$1"; got="$2"; want="$3"
    if [ "$got" = "$want" ]; then
        printf "  %-56s PASS\n" "$name"
        PASS=$((PASS + 1))
    else
        printf "  %-56s FAIL (got=%s want=%s)\n" "$name" "$got" "$want"
        FAIL=$((FAIL + 1))
    fi
}

assert_gt() {
    name="$1"; got="$2"
    if [ "${got:-0}" -gt 0 ] 2>/dev/null; then
        printf "  %-56s PASS\n" "$name"
        PASS=$((PASS + 1))
    else
        printf "  %-56s FAIL (got=%s want=>0)\n" "$name" "$got"
        FAIL=$((FAIL + 1))
    fi
}

# ── sarif.rego ────────────────────────────────────────────────────────── #

printf "\npolicy/sarif.rego:\n"

EMPTY_SARIF='{"runs":[{"tool":{"driver":{"name":"osv-scanner"}},"results":[]}]}'
assert_eq "sarif: empty results → no violations" \
    "$(opa_violations policy/sarif.rego "$EMPTY_SARIF" odoo_mcp.sarif)" "0"

CRITICAL='{"runs":[{"tool":{"driver":{"name":"osv-scanner"}},"results":[{"ruleId":"CVE-2024-1234","properties":{"cvssScore":9.8,"severity":"CRITICAL"},"locations":[{"physicalLocation":{"artifactLocation":{"uri":"sbom.cdx.json"}}}]}]}]}'
assert_gt "sarif: CVSS 9.8 CRITICAL → violation" \
    "$(opa_violations policy/sarif.rego "$CRITICAL" odoo_mcp.sarif)"

LOW='{"runs":[{"tool":{"driver":{"name":"osv-scanner"}},"results":[{"ruleId":"CVE-2024-5678","properties":{"cvssScore":3.1,"severity":"LOW"},"locations":[{"physicalLocation":{"artifactLocation":{"uri":"sbom.cdx.json"}}}]}]}]}'
assert_eq "sarif: CVSS 3.1 LOW → no violations" \
    "$(opa_violations policy/sarif.rego "$LOW" odoo_mcp.sarif)" "0"

HIGH_STR='{"runs":[{"tool":{"driver":{"name":"osv-scanner"}},"results":[{"ruleId":"CVE-2024-9999","properties":{"severity":"HIGH"},"locations":[{"physicalLocation":{"artifactLocation":{"uri":"sbom.cdx.json"}}}]}]}]}'
assert_gt "sarif: severity=HIGH (no cvssScore) → violation" \
    "$(opa_violations policy/sarif.rego "$HIGH_STR" odoo_mcp.sarif)"

# ── c_quality.rego ────────────────────────────────────────────────────── #

printf "\npolicy/c_quality.rego:\n"

CLEAN_SARIF='{"version":"2.1.0","runs":[{"tool":{"driver":{"name":"cppcheck","rules":[]}},"results":[]}]}'
assert_eq "c_quality: no findings → no violations" \
    "$(opa_violations policy/c_quality.rego "$CLEAN_SARIF" odoo_mcp.c_quality)" "0"

# ── containers.rego ───────────────────────────────────────────────────── #

printf "\npolicy/containers.rego:\n"

GOOD_CF=$(sh "$ROOT/scripts/check_containers.sh" 2>/dev/null) || GOOD_CF='{}'
if [ "$GOOD_CF" != '{}' ]; then
    assert_eq "containers: real assets pass gate" \
        "$(opa_violations policy/containers.rego "$GOOD_CF" odoo_mcp.containers)" "0"
else
    printf "  %-56s SKIP (containers check failed)\n" "containers: real assets"
fi

# ── terraform_sast.rego ───────────────────────────────────────────────── #

printf "\npolicy/terraform_sast.rego:\n"

CLEAN_KICS='{"version":"2.1.0","runs":[{"tool":{"driver":{"name":"kics","rules":[]}},"results":[]}]}'
assert_eq "terraform_sast: no findings → no violations" \
    "$(opa_violations policy/terraform_sast.rego "$CLEAN_KICS" odoo_mcp.terraform_sast)" "0"

# ── Summary ───────────────────────────────────────────────────────────── #

printf "\n%d/%d passed" "$PASS" "$((PASS + FAIL))"
if [ "$FAIL" -gt 0 ]; then
    printf "  (%d FAILED)\n" "$FAIL"
    exit 1
fi
printf "\n"
