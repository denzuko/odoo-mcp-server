## Summary

What does this PR do? Reference any related issue with `Fixes #N`.

## Build

```
$ cc -o nob nob.c && ./nob

```

## cppcheck

```
$ cppcheck --enable=all --std=c99 \
    --suppress=missingInclude --suppress=missingIncludeSystem *.c

```

## OPA gates

```
$ opa eval -d policy/c_quality.rego -i cppcheck.sarif.json \
    --fail-defined 'data.odoo_mcp.c_quality.violations[_]'

$ conftest test terraform/plan.json \
    --policy policy/ --namespace odoo_mcp.terraform_plan --output table

```

## Checklist

- [ ] `./nob` succeeds (native build)
- [ ] cppcheck reports zero findings
- [ ] All Rego gates pass
- [ ] `CHANGELOG.md` updated under `[Unreleased]`
- [ ] No `#ifdef __wasm__` added outside `net.h`
- [ ] No `system()`, `popen()`, or `exec*()` calls introduced
- [ ] `LICENSE.txt` updated if new vendored dep added
