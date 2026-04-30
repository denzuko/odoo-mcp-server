#!/usr/bin/env python3
"""scripts/clang_tidy_to_sarif.py — convert clang-tidy text output to SARIF.
Usage: clang-tidy ... 2>&1 | python3 scripts/clang_tidy_to_sarif.py clang-tidy.sarif.json
"""
import re, json, sys

dst = sys.argv[1]

results = []
for line in sys.stdin:
    m = re.match(r'(.+):(\d+):\d+: (error|warning|note): (.+) \[(.+)\]', line)
    if m:
        results.append({
            'ruleId':  m.group(5),
            'level':   'error' if m.group(3) == 'error' else 'warning',
            'message': {'text': m.group(4)},
            'locations': [{'physicalLocation': {
                'artifactLocation': {'uri': m.group(1)},
                'region': {'startLine': int(m.group(2))},
            }}],
        })

sarif = {
    'version': '2.1.0',
    'runs': [{'tool': {'driver': {'name': 'clang-tidy', 'rules': []}},
              'results': results}],
}
json.dump(sarif, open(dst, 'w'), indent=2)
print(f'clang-tidy: {len(results)} findings')
