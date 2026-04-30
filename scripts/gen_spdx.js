#!/usr/bin/env node
// scripts/gen_spdx.js — generate SPDX 2.3 JSON from CycloneDX sbom.cdx.json
// Used by CI when cdxgen --output-format spdx-json is unavailable
'use strict';
const fs = require('fs');
const c  = JSON.parse(fs.readFileSync('sbom.cdx.json', 'utf8'));
const s  = {
  spdxVersion:       'SPDX-2.3',
  dataLicense:       'CC0-1.0',
  SPDXID:            'SPDXRef-DOCUMENT',
  name:              c.metadata.component.name,
  documentNamespace: 'https://github.com/denzuko/odoo-mcp-server/sbom',
  packages: (c.components || []).map((p, i) => ({
    SPDXID:           'SPDXRef-' + p.name + '-' + i,
    name:             p.name,
    versionInfo:      p.version || 'NOASSERTION',
    licenseConcluded: p.licenses?.[0]?.license?.id || 'NOASSERTION',
    downloadLocation: 'NOASSERTION',
    filesAnalyzed:    false,
  })),
};
fs.writeFileSync('sbom.spdx.json', JSON.stringify(s, null, 2));
console.log('SPDX:', s.packages.length, 'packages');
