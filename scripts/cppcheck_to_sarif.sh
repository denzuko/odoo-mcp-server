#!/bin/sh
# scripts/cppcheck_to_sarif.sh
# Convert cppcheck --xml output to SARIF 2.1.0.
# startLine clamped to >= 1 (SARIF spec requirement).
#
# Usage: sh scripts/cppcheck_to_sarif.sh cppcheck.xml cppcheck.sarif.json
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

set -eu

SRC="${1:-cppcheck.xml}"
DST="${2:-cppcheck.sarif.json}"
STUB='{"version":"2.1.0","runs":[{"tool":{"driver":{"name":"cppcheck","rules":[]}},"results":[]}]}'

[ -f "$SRC" ] || { printf '%s\n' "$STUB" > "$DST"; echo "cppcheck: no input"; exit 0; }

# Parse cppcheck XML with awk, emit SARIF JSON.
# cppcheck --xml format: <error id="..." severity="..." msg="...">
#                          <location file="..." line="N"/>
#                        </error>
awk '
BEGIN {
    print "{\"version\":\"2.1.0\",\"runs\":[{\"tool\":{\"driver\":{\"name\":\"cppcheck\",\"rules\":[]}},\"results\":["
    first = 1
    count = 0
}

/<error / {
    # Extract attributes
    id  = "unknown"; sev = ""; msg = ""
    if (match($0, /id="([^"]*)"/, a))   id  = a[1]
    if (match($0, /severity="([^"]*)"/, a)) sev = a[1]
    if (match($0, /msg="([^"]*)"/, a))  msg = a[1]
    # escape backslash then double-quote in msg
    gsub(/\\/, "\\\\", msg); gsub(/"/, "\\\"", msg)
    pending_id  = id
    pending_sev = sev
    pending_msg = msg
    pending_file = ""
    pending_line = 1
}

/<location / {
    f = ""; ln = 0
    if (match($0, /file="([^"]*)"/, a)) f  = a[1]
    if (match($0, /line="([^"]*)"/, a)) ln = int(a[1])
    if (ln < 1) ln = 1     # clamp — SARIF requires startLine >= 1
    pending_file = f
    pending_line = ln
}

/<\/error>/ {
    level = (pending_sev == "error" || pending_sev == "warning") ? "error" : "note"
    if (!first) printf ","
    first = 0
    printf "{\"ruleId\":\"%s\",\"level\":\"%s\",\"message\":{\"text\":\"%s\"},",
        pending_id, level, pending_msg
    printf "\"properties\":{\"severity\":\"%s\"},", toupper(pending_sev)
    printf "\"locations\":[{\"physicalLocation\":{\"artifactLocation\":{\"uri\":\"%s\"},",
        pending_file
    printf "\"region\":{\"startLine\":%d}}}]}", pending_line
    count++
}

END {
    print "]}]}"
    print count " findings" > "/dev/stderr"
}
' "$SRC" > "$DST"

echo "cppcheck: $(grep -c '"ruleId"' "$DST" || echo 0) findings written to $DST"
