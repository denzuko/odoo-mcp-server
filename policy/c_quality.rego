# policy/c_quality.rego — C code quality gate
#
# Consumes SARIF 2.1.0 produced by:
#   cppcheck --enable=all --xml 2>cppcheck.xml
#   codechecker analyze compile_commands.json -o reports/
#   codechecker parse reports/ --export sarif -o quality.sarif.json
#   clang-tidy *.c -- -std=c99 ... (via compile_commands.json)
#
# AST-backed checks from clang-tidy and clang static analyser surface:
#   - buffer overflows, use-after-free, null dereference  (security)
#   - memory leaks, uninitialised reads                   (correctness)
#   - style violations against our C99/tsoding conventions (quality)
#
# OPA gate: any violation in blocked_rules or above cvss_block
# causes CI to fail with a descriptive message.
#
# Run:
#   opa eval -d policy/c_quality.rego -i quality.sarif.json \
#     --fail-defined 'data.odoo_mcp.c_quality.violations[_]'
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

package odoo_mcp.c_quality

import future.keywords.if
import future.keywords.in

# ── Thresholds ─────────────────────────────────────────────────────────── #

# clang-tidy / cppcheck rule IDs that are hard-blocks regardless of severity.
# These map directly to AST-level checks the tools emit.
blocked_rules := {
    # Memory safety — clang static analyser
    "clang-analyzer-core.NullDereference",
    "clang-analyzer-core.UndefinedBinaryOperatorResult",
    "clang-analyzer-unix.Malloc",
    "clang-analyzer-unix.MallocSizeof",
    "clang-analyzer-security.insecureAPI.strcpy",
    "clang-analyzer-security.insecureAPI.gets",
    "clang-analyzer-security.insecureAPI.sprintf",
    "clang-analyzer-alpha.security.ArrayBound",
    "clang-analyzer-alpha.security.ReturnPtrRange",

    # Buffer handling — cppcheck
    "bufferAccessOutOfBounds",
    "bufferAccessOutOfBoundsList",
    "outOfBounds",
    "negativeIndex",
    "writeReadOnlyFile",

    # Use-after-free / double-free
    "clang-analyzer-cplusplus.NewDelete",
    "doubleFree",
    "useAfterFree",

    # Format string injection
    "clang-analyzer-security.insecureAPI.vfork",
    "cstyleCast",

    # Uninitialized reads
    "clang-analyzer-core.uninitialized.Assign",
    "clang-analyzer-core.uninitialized.Branch",
    "uninitvar",

    # Our style: forbidden functions (checked via clang-tidy modernize)
    # Any call to banned APIs surfaces as one of these rule IDs
    "clang-analyzer-security.insecureAPI.rand",
    "clang-analyzer-security.insecureAPI.mktemp",
}

# SARIF severity levels that are always hard-blocks
blocked_levels := {"error"}

# CVSS score at or above which we block (for OSV/CVE findings if mixed in)
cvss_block_threshold := 7.0

# ── C99 / tsoding style rules ──────────────────────────────────────────── #
# These are warnings we enforce as errors via clang-tidy checks.
# Violations here fail the gate with a style message.

style_rules := {
    # Yoda conditions: we enforce them via a custom check or
    # clang-tidy readability-yoda-conditions (if configured)
    "readability-yoda-conditions",

    # Magic numbers without #define — caught by cppcheck
    "constParameter",

    # Functions longer than 60 lines — caught by clang-tidy
    "readability-function-size",
}

# ── Violations ────────────────────────────────────────────────────────────── #

# Security / correctness hard blocks
violations[msg] if {
    run    := input.runs[_]
    result := run.results[_]
    result.ruleId in blocked_rules
    loc    := result.locations[0].physicalLocation
    msg := sprintf("BLOCKED rule %v in %v:%v — %v",
        [result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

# Severity-level blocks (error level from any tool)
violations[msg] if {
    run    := input.runs[_]
    result := run.results[_]
    result.level in blocked_levels
    not result.ruleId in blocked_rules   # avoid duplicate msgs
    loc    := result.locations[0].physicalLocation
    msg := sprintf("ERROR level finding: %v in %v:%v — %v",
        [result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

# Style rule violations (treated as errors in our pipeline)
violations[msg] if {
    run    := input.runs[_]
    result := run.results[_]
    result.ruleId in style_rules
    loc    := result.locations[0].physicalLocation
    msg := sprintf("STYLE violation %v in %v:%v — %v",
        [result.ruleId,
         loc.artifactLocation.uri,
         loc.region.startLine,
         result.message.text])
}

# CVSS-scored CVE findings (from osv-scanner if mixed into SARIF)
violations[msg] if {
    run    := input.runs[_]
    result := run.results[_]
    cvss   := to_number(object.get(result.properties, "cvss", "0"))
    cvss   >= cvss_block_threshold
    msg := sprintf("CVE CVSS %.1f >= %.1f: %v — %v",
        [cvss, cvss_block_threshold,
         result.ruleId, result.message.text])
}

# ── Summary ───────────────────────────────────────────────────────────────── #

# Count of violations — useful for reporting
violation_count := count(violations)

# Pass/fail summary for CI reporting
summary := {
    "pass":            count(violations) == 0,
    "violation_count": count(violations),
    "violations":      violations,
}
