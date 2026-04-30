#!/usr/bin/env python3
"""scripts/sarif_stub.py — ensure a named SARIF file exists and is valid.
Usage: python3 scripts/sarif_stub.py <file.sarif.json> [tool-name]
Writes a minimal valid SARIF 2.1.0 stub if the file is missing or empty.
"""
import json, os, sys

path     = sys.argv[1]
toolname = sys.argv[2] if len(sys.argv) > 2 else os.path.basename(path)

if os.path.exists(path) and os.path.getsize(path) > 0:
    try:
        d     = json.load(open(path))
        total = sum(len(r.get('results', [])) for r in d.get('runs', []))
        print(f'{toolname}: {total} findings')
        sys.exit(0)
    except json.JSONDecodeError:
        pass  # fall through to stub

stub = {'version': '2.1.0', 'runs': []}
json.dump(stub, open(path, 'w'), indent=2)
print(f'{toolname}: no findings (stub written)')
