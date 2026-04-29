# Support

## How to get help

### GitHub Issues

For bug reports and feature requests, open an issue:

https://github.com/denzuko/odoo-mcp-server/issues

Please include:

- Your OS and version (FreeBSD/Linux)
- Your Odoo version
- The MCP client you are using (Claude Desktop, etc.)
- Any error output from the server process
- Steps to reproduce the problem

### Email

For questions not suited to a public issue — deployment help,
integration questions, general usage:

**denzuko@dapla.net**

Response time is best-effort; typically within a few days.

### Security Issues

Do **not** use GitHub Issues for security vulnerabilities.
See [SECURITY.md](SECURITY.md).

## What is not supported

- Odoo instances older than version 15
- Plaintext HTTP to Odoo (HTTPS only)
- Platforms other than FreeBSD, Linux, and wasm32-wasi
- Concurrent multi-session state (stateless by design)

## Useful references

- `README.md` — build and deployment quickstart
- `CONTRIBUTING.md` — how to submit a fix yourself
- [GH Pages](https://denzuko.github.io/odoo-mcp-server/) — full docs
