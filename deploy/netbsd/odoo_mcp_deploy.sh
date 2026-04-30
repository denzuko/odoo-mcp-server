#!/bin/sh
# deploy/netbsd/odoo_mcp_deploy.sh — production deploy on NetBSD
#
# NetBSD variant of the FreeBSD deploy script.
# Uses ftp(1) (NetBSD's fetch tool), pkgin for cosign.
# rc.d unit uses NetBSD rc.subr conventions.
#
# Usage (as root):
#   ./odoo_mcp_deploy.sh --tag v1.10.0
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

set -eu

MCP_TAG="${MCP_TAG:-latest}"
MCP_USER="${MCP_USER:-odoo_mcp}"
MCP_UID="${MCP_UID:-2002}"
GITHUB_REPO="denzuko/odoo-mcp-server"
INSTALL_BIN="/usr/local/sbin/odoo-mcp-server"
RCD_UNIT="/etc/rc.d/odoo_mcp"
CONF_DIR="/usr/local/etc/odoo-mcp"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

log()  { printf '[odoo-mcp-deploy] %s\n' "$*"; }
die()  { printf '[odoo-mcp-deploy] FATAL: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "Required: $1 (pkgin install $1)"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --tag) shift; MCP_TAG="$1" ;;
        *)     die "Unknown: $1" ;;
    esac
    shift
done

[ "$(id -u)" -eq 0 ] || die "Must be run as root"
need ftp        # NetBSD ftp(1) handles HTTPS
need cosign
need service

log "SECTION 5: Credentials"
if [ -z "${ODOO_URL:-}" ];     then printf 'ODOO_URL: '; read -r ODOO_URL; fi
if [ -z "${ODOO_DB:-}" ];      then printf 'ODOO_DB: '; read -r ODOO_DB; fi
if [ -z "${ODOO_USER:-}" ];    then printf 'ODOO_USER: '; read -r ODOO_USER; fi
if [ -z "${ODOO_API_KEY:-}" ]; then
    printf 'ODOO_API_KEY: '; stty -echo; read -r ODOO_API_KEY; stty echo; echo
fi

log "SECTION 6: Service account"
if ! id "${MCP_USER}" >/dev/null 2>&1; then
    useradd -u "${MCP_UID}" -d /nonexistent -s /sbin/nologin \
            -c "odoo-mcp-server service" "${MCP_USER}"
fi

log "SECTION 7: Download + verify ${MCP_TAG}"
BASE="https://github.com/${GITHUB_REPO}/releases/download/${MCP_TAG}"
TMPDIR="$(mktemp -d)"

ftp -o "${TMPDIR}/odoo-mcp-server"        "${BASE}/odoo-mcp-server"
ftp -o "${TMPDIR}/odoo-mcp-server.bundle" "${BASE}/odoo-mcp-server.bundle"

cosign verify-blob \
    --bundle "${TMPDIR}/odoo-mcp-server.bundle" \
    --certificate-identity \
      "https://github.com/${GITHUB_REPO}/.github/workflows/ci.yml@refs/tags/${MCP_TAG}" \
    --certificate-oidc-issuer \
      "https://token.actions.githubusercontent.com" \
    "${TMPDIR}/odoo-mcp-server" \
|| die "Signature verification failed"

install -m 0555 -o root -g wheel \
    "${TMPDIR}/odoo-mcp-server" "${INSTALL_BIN}"
rm -rf "${TMPDIR}"
log "Installed ${INSTALL_BIN}"

log "SECTION 9: Config"
mkdir -p "${CONF_DIR}"
chmod 750 "${CONF_DIR}"; chown root:"${MCP_USER}" "${CONF_DIR}"
_env="${CONF_DIR}/odoo-mcp.env"
if [ ! -f "${_env}" ]; then
    install -m 0640 -o root -g "${MCP_USER}" /dev/null "${_env}"
    printf 'ODOO_URL=%s\nODOO_DB=%s\nODOO_USER=%s\nODOO_API_KEY=%s\nHOST=127.0.0.1\nPORT=8000\n' \
        "${ODOO_URL}" "${ODOO_DB}" "${ODOO_USER}" "${ODOO_API_KEY}" > "${_env}"
fi

log "SECTION 10: rc.d unit"
install -m 0555 -o root -g wheel \
    "${SCRIPT_DIR}/odoo_mcp.rc" "${RCD_UNIT}"
grep -q "odoo_mcp=YES" /etc/rc.conf 2>/dev/null \
    || echo 'odoo_mcp=YES' >> /etc/rc.conf

log "SECTION 11: Start"
service odoo_mcp restart

log "=== odoo-mcp-server production (NetBSD) running — ${MCP_TAG} ==="
