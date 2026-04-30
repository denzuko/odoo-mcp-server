#!/bin/sh
# scripts/check_containers.sh
# Reads containers/ directory and emits JSON for policy/containers.rego.
# Usage: sh scripts/check_containers.sh > containers_check.json
#        opa eval -d policy/containers.rego -i containers_check.json \
#          --fail-defined 'data.odoo_mcp.containers.violations[_]'
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONTAINERS="$ROOT/containers"

# Escape a file's content for use as a JSON string value (POSIX awk)
json_escape() {
    awk 'BEGIN { printf "\"" }
         { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); gsub(/\t/, "\\t")
           gsub(/\r/, "\\r"); printf "%s\\n", $0 }
         END { printf "\"" }'
}

printf '{\n'

# Containerfile
printf '  "containerfile": {"name": "Containerfile", "content": %s},\n' \
    "$(json_escape < "$CONTAINERS/Containerfile")"

# All unit files
printf '  "units": [\n'
first=1
for f in "$CONTAINERS"/*.container "$CONTAINERS"/*.network "$CONTAINERS"/*.volume; do
    [ -f "$f" ] || continue
    name="$(basename "$f")"
    if [ "$first" = "1" ]; then
        first=0
    else
        printf ',\n'
    fi
    printf '    {"name": "%s", "content": %s}' \
        "$name" "$(json_escape < "$f")"
done
printf '\n  ]\n'

printf '}\n'
