# Contributing to odoo-mcp-server

Thanks for taking the time. This is a focused project — contributions
are welcome but scope is intentionally narrow.

## Ways to contribute

- Bug reports via [GitHub Issues](https://github.com/denzuko/odoo-mcp-server/issues)
- Bug fixes via pull request
- Documentation corrections
- Security issues — see [SECURITY.md](SECURITY.md) before opening a ticket

## Before you open a PR

1. **Run policy checks:**

   ```sh
   cc -o nob nob.c && ./nob
   opa check policy/
   opa eval -d policy/c_quality.rego -i cppcheck.sarif.json \
     --fail-defined 'data.odoo_mcp.c_quality.violations[_]'
   ```

2. **Run cppcheck — zero findings expected:**

   ```sh
   cppcheck --enable=all --std=c99 \
     --suppress=missingInclude --suppress=missingIncludeSystem \
     *.c
   ```

3. **Regenerate SBOM if deps changed:**

   ```sh
   cdxgen -t c -o sbom.cdx.json --spec-version 1.6 .
   ```

4. **Update `CHANGELOG.md`** under an `[Unreleased]` header.

## Code style

This project follows the conventions in `main.c` and `mcp.c`:

- C99 (`-std=c99`)
- Yoda conditions: `if (NULL == p)` not `if (!p)`
- Arena allocator for all per-request memory — no `malloc` in hot path
- Single-responsibility: `net.h` owns transport, `odoo.c` owns XML-RPC,
  `mcp.c` owns JSON-RPC, `json.h` owns serialisation
- No `system()`, `popen()`, or `exec*()` — enforced by Rego AST gate
- `#ifdef __wasm__` appears **only** in `net.h` — nowhere else

## Commit messages

```
type: short summary (72 chars max)

Longer explanation if needed. Wrap at 72 chars.
```

Types: `feat`, `fix`, `test`, `docs`, `chore`, `refactor`, `security`

## Branch and review

- Branch off `main`: `fix/description` or `feat/description`
- One logical change per PR
- All paths owned by `@denzuko` (see `CODEOWNERS`) — review required
- Linear history enforced; rebase before requesting review

## Getting help

- GitHub Issues: https://github.com/denzuko/odoo-mcp-server/issues
- Email: denzuko@dapla.net

## Code of Conduct

See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
