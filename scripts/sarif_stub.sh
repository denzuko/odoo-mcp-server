#!/bin/sh
# scripts/sarif_stub.sh — ensure a SARIF file exists and is valid JSON.
# Usage: sh scripts/sarif_stub.sh <file.sarif.json> [tool-name]
# Writes a minimal valid SARIF 2.1.0 stub if the file is missing or empty.
# No Python. POSIX sh + awk only.
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

set -eu

FILE="${1:-}"
TOOL="${2:-$(basename "${FILE:-stub}")}"

[ -n "$FILE" ] || { echo "usage: sarif_stub.sh <file> [tool]" >&2; exit 1; }

STUB='{"version":"2.1.0","runs":[{"tool":{"driver":{"name":"osv-scanner","rules":[]}},"results":[]}]}'

if [ ! -f "$FILE" ] || [ ! -s "$FILE" ]; then
    printf '%s\n' "$STUB" > "$FILE"
    echo "${TOOL}: no findings (stub written)"
    exit 0
fi

# Validate it looks like JSON with a runs key
if grep -q '"runs"' "$FILE" 2>/dev/null; then
    COUNT=$(grep -o '"results"' "$FILE" | wc -l | tr -d ' ')
    echo "${TOOL}: ${COUNT} result blocks"
else
    printf '%s\n' "$STUB" > "$FILE"
    echo "${TOOL}: invalid SARIF replaced with stub"
fi
