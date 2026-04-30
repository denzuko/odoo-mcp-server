#!/usr/bin/env python3
"""scripts/cppcheck_to_sarif.py — convert cppcheck --xml output to SARIF 2.1.0.
Usage: python3 scripts/cppcheck_to_sarif.py cppcheck.xml cppcheck.sarif.json
"""
import xml.etree.ElementTree as ET
import json, sys

src  = sys.argv[1]
dst  = sys.argv[2]

try:
    tree = ET.parse(src)
except (FileNotFoundError, ET.ParseError) as e:
    print(f'cppcheck_to_sarif: {e} — writing empty SARIF')
    json.dump({'version': '2.1.0', 'runs': []}, open(dst, 'w'), indent=2)
    sys.exit(0)

results = []
for err in tree.findall('.//error'):
    loc = err.find('location')
    results.append({
        'ruleId':  err.get('id', 'unknown'),
        'level':   'error' if err.get('severity') in ('error', 'warning') else 'note',
        'message': {'text': err.get('msg', '')},
        'properties': {'severity': err.get('severity', '').upper()},
        'locations': [{'physicalLocation': {
            'artifactLocation': {
                'uri': loc.get('file', '') if loc is not None else '',
            },
            'region': {
                'startLine': int(loc.get('line', 1)) if loc is not None else 1,
            },
        }}],
    })

sarif = {
    'version': '2.1.0',
    'runs': [{'tool': {'driver': {'name': 'cppcheck', 'rules': []}},
              'results': results}],
}
json.dump(sarif, open(dst, 'w'), indent=2)
print(f'cppcheck: {len(results)} findings')
