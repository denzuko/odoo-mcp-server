#!/bin/sh
# deploy/freebsd/odoo_mcp_deploy.sh — production deploy on FreeBSD
#
# Downloads the signed ELF from GitHub Release, verifies with cosign,
# installs under /usr/local/sbin, installs rc.d unit, starts service.
#
# Usage (as root on FreeBSD):
#   ./odoo_mcp_deploy.sh --tag v1.10.0
#
# Environment overrides:
#   ODOO_URL, ODOO_DB, ODOO_USER, ODOO_API_KEY
#   MCP_TAG, MCP_USER, MCP_UID
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

set -eu

# ---------------------------------------------------------------------------
# SECTION 1: Defaults
# ---------------------------------------------------------------------------
MCP_TAG="${MCP_TAG:-latest}"
MCP_USER="${MCP_USER:-odoo_mcp}"
MCP_UID="${MCP_UID:-2002}"
GITHUB_REPO="denzuko/odoo-mcp-server"
INSTALL_BIN="/usr/local/sbin/odoo-mcp-server"
RCD_UNIT="/usr/local/etc/rc.d/odoo_mcp"
CONF_DIR="/usr/local/etc/odoo-mcp"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ---------------------------------------------------------------------------
# SECTION 2: Helpers
# ---------------------------------------------------------------------------
log()  { printf '[odoo-mcp-deploy] %s\n' "$*"; }
die()  { printf '[odoo-mcp-deploy] FATAL: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "Required: $1 (pkg install $1)"; }

# ---------------------------------------------------------------------------
# SECTION 3: Argument parsing
# ---------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        --tag) shift; MCP_TAG="$1" ;;
        *)     die "Unknown argument: $1" ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# SECTION 4: Privilege check + tools
# ---------------------------------------------------------------------------
log "SECTION 4: Privilege check + tools"
[ "$(id -u)" -eq 0 ] || die "Must be run as root"
need fetch      # FreeBSD fetch(1)
need cosign     # security/cosign port
need service    # FreeBSD service(8)

# ---------------------------------------------------------------------------
# SECTION 5: Credentials
# ---------------------------------------------------------------------------
log "SECTION 5: Credentials"

if [ -z "${ODOO_URL:-}" ]; then
    printf 'ODOO_URL: '; read -r ODOO_URL
fi
if [ -z "${ODOO_DB:-}" ]; then
    printf 'ODOO_DB: '; read -r ODOO_DB
fi
if [ -z "${ODOO_USER:-}" ]; then
    printf 'ODOO_USER: '; read -r ODOO_USER
fi
if [ -z "${ODOO_API_KEY:-}" ]; then
    printf 'ODOO_API_KEY: '; stty -echo; read -r ODOO_API_KEY; stty echo; echo
fi

[ -n "${ODOO_URL}" ]     || die "ODOO_URL required"
[ -n "${ODOO_DB}" ]      || die "ODOO_DB required"
[ -n "${ODOO_USER}" ]    || die "ODOO_USER required"
[ -n "${ODOO_API_KEY}" ] || die "ODOO_API_KEY required"

# ---------------------------------------------------------------------------
# SECTION 6: Service account
# ---------------------------------------------------------------------------
log "SECTION 6: Service account"

if ! pw user show "${MCP_USER}" >/dev/null 2>&1; then
    pw useradd "${MCP_USER}" \
        -u "${MCP_UID}" \
        -d /nonexistent \
        -s /usr/sbin/nologin \
        -c "odoo-mcp-server production service account"
    log "Created user ${MCP_USER}"
else
    log "User ${MCP_USER} exists — skipping"
fi

# ---------------------------------------------------------------------------
# SECTION 7: Download + verify from GitHub Release
# ---------------------------------------------------------------------------
log "SECTION 7: Download release ${MCP_TAG} from GitHub"

BASE="https://github.com/${GITHUB_REPO}/releases/download/${MCP_TAG}"
TMPDIR="$(mktemp -d)"

fetch -o "${TMPDIR}/odoo-mcp-server"        "${BASE}/odoo-mcp-server"
fetch -o "${TMPDIR}/odoo-mcp-server.bundle" "${BASE}/odoo-mcp-server.bundle"

log "Verifying cosign signature..."
cosign verify-blob \
    --bundle "${TMPDIR}/odoo-mcp-server.bundle" \
    --certificate-identity \
      "https://github.com/${GITHUB_REPO}/.github/workflows/ci.yml@refs/tags/${MCP_TAG}" \
    --certificate-oidc-issuer \
      "https://token.actions.githubusercontent.com" \
    "${TMPDIR}/odoo-mcp-server" \
|| die "Signature verification failed — refusing to install"

log "Signature verified."

# ---------------------------------------------------------------------------
# SECTION 8: Install binary
# ---------------------------------------------------------------------------
log "SECTION 8: Install binary → ${INSTALL_BIN}"

install -m 0555 -o root -g wheel \
    "${TMPDIR}/odoo-mcp-server" "${INSTALL_BIN}"

rm -rf "${TMPDIR}"
log "Installed ${INSTALL_BIN}"

# ---------------------------------------------------------------------------
# SECTION 9: Config directory + env file
# ---------------------------------------------------------------------------
log "SECTION 9: Config"
mkdir -p "${CONF_DIR}"
chmod 750 "${CONF_DIR}"
chown root:"${MCP_USER}" "${CONF_DIR}"

_env="${CONF_DIR}/odoo-mcp.env"
if [ ! -f "${_env}" ]; then
    install -m 0640 -o root -g "${MCP_USER}" /dev/null "${_env}"
    cat > "${_env}" << ENVEOF
# ${_env} — generated by odoo_mcp_deploy.sh $(date -u +%Y-%m-%dT%H:%M:%SZ)
# Edit carefully — contains credentials.

ODOO_URL=${ODOO_URL}
ODOO_DB=${ODOO_DB}
ODOO_USER=${ODOO_USER}
ODOO_API_KEY=${ODOO_API_KEY}

HOST=127.0.0.1
PORT=8000
CORS_ORIGINS=https://claude.ai
ENVEOF
    log "Wrote ${_env}"
else
    log "Env file exists — preserving operator edits"
fi

# ---------------------------------------------------------------------------
# SECTION 10: rc.d unit
# ---------------------------------------------------------------------------
log "SECTION 10: rc.d unit → ${RCD_UNIT}"

install -m 0555 -o root -g wheel \
    "${SCRIPT_DIR}/odoo_mcp.rc" "${RCD_UNIT}"

# Enable in rc.conf if not already set
if ! grep -q "odoo_mcp_enable" /etc/rc.conf 2>/dev/null; then
    echo 'odoo_mcp_enable="YES"' >> /etc/rc.conf
    log "Added odoo_mcp_enable=YES to /etc/rc.conf"
fi

# ---------------------------------------------------------------------------
# SECTION 11: Start service
# ---------------------------------------------------------------------------
log "SECTION 11: Start"
service odoo_mcp restart

_elapsed=0
until fetch -qo - http://127.0.0.1:8000/healthz >/dev/null 2>&1; do
    [ "${_elapsed}" -ge 30 ] && die "/healthz no response after 30s"
    sleep 2; _elapsed=$((_elapsed + 2))
done

log ""
log "=== odoo-mcp-server production running (FreeBSD) ==="
log "  Release    : ${MCP_TAG}"
log "  Binary     : ${INSTALL_BIN}"
log "  MCP        : http://127.0.0.1:8000/mcp"
log "  Health     : http://127.0.0.1:8000/healthz"
log "  Logs       : service odoo_mcp logs"
