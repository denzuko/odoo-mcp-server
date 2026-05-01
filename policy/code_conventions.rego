# policy/code_conventions.rego — C99 code conventions gate
#
# Consumes ast_calls.json produced by the sast-c CI job:
#   clang -std=c99 -I. -Xclang -ast-dump=json -fsyntax-only \
#     main.c mcp.c odoo.c 2>/dev/null | \
#   jq '[.. | select(.kind?=="CallExpr") |
#     {callee:(.inner[0].referencedDecl.name? //
#              .inner[0].inner[0].referencedDecl.name?)}
#     | select(.callee!=null)]' > ast_calls.json
#
# Input schema: array of {callee: "function_name"} objects
#
# Usage:
#   opa eval -d policy/code_conventions.rego -i ast_calls.json \
#     --fail-defined 'data.odoo_mcp.code_conventions.violations[_]'
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

package odoo_mcp.code_conventions

import future.keywords.contains
import future.keywords.if
import future.keywords.in

# ── Banned function calls (security + project rules) ──────────────────── #

# These must never appear in production C source.
# OPA gate hard-blocks any plan where ast_calls.json contains them.
banned_calls := {
    # Shell injection / arbitrary execution — project rule
    "system",
    "popen",
    "execv",
    "execve",
    "execvp",
    "execl",
    "execlp",
    "execle",
    "execvpe",
    "posix_spawn",

    # Unsafe string functions — buffer overflow risk
    "gets",
    "sprintf",     # use snprintf
    "strcpy",      # use strlcpy or arena_strdup
    "strcat",      # use strlcat
    "vsprintf",    # use vsnprintf

    # Unsafe temp files
    "mktemp",
    "tmpnam",

    # Deprecated randomness (not cryptographic)
    "rand",
    "srand",

    # Direct heap allocation — must use arena or rc
    "malloc",
    "calloc",
    "realloc",
    "free",
}

# ── Violations ────────────────────────────────────────────────────────── #

violations contains msg if {
    call := input[_]
    call.callee in banned_calls
    msg := sprintf(
        "BANNED call: %v() — use project-approved alternative",
        [call.callee])
}

# ── Summary ───────────────────────────────────────────────────────────── #

summary := {
    "pass":            count(violations) == 0,
    "violation_count": count(violations),
}
