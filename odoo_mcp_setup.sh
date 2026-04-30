#!/bin/sh
# odoo_mcp_setup.sh — rootless Podman Quadlet installer for odoo-mcp-server
#
# Mirrors fts-quadlet-setup/fts_setup.sh conventions.
# Must be run as root. Creates a dedicated service account (odoo-mcp),
# installs quadlet unit files under its home, and starts the service.
#
# Usage:
#   ./odoo_mcp_setup.sh [--ip HOST_IP]
#
# Environment overrides:
#   ODOO_URL, ODOO_DB, ODOO_USER, ODOO_API_KEY
#   MCP_USER, MCP_UID, MCP_PORT
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

set -eu

# ---------------------------------------------------------------------------
# SECTION 1: Configuration defaults
# ---------------------------------------------------------------------------
MCP_USER="${MCP_USER:-odoo-mcp}"
MCP_UID="${MCP_UID:-2002}"
MCP_PORT="${MCP_PORT:-8000}"
BUS_TIMEOUT=30
QUADLET_DIR="/home/${MCP_USER}/.config/containers/systemd"
IMAGE="ghcr.io/denzuko/odoo-mcp-server:latest"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ---------------------------------------------------------------------------
# SECTION 2: Helpers
# ---------------------------------------------------------------------------
log()  { printf '[odoo-mcp-setup] %s\n' "$*"; }
die()  { printf '[odoo-mcp-setup] FATAL: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "Required tool not found: $1"; }

as_mcp() { su -s /bin/sh -c "$1" "$MCP_USER"; }

# ---------------------------------------------------------------------------
# SECTION 3: Argument parsing
# ---------------------------------------------------------------------------
HOST_IP=""

while [ $# -gt 0 ]; do
    case "$1" in
        --ip) shift; HOST_IP="$1" ;;
        *)    die "Unknown argument: $1" ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# SECTION 4: Privilege check
# ---------------------------------------------------------------------------
log "SECTION 4: Privilege check"
[ "$(id -u)" -eq 0 ] || die "Must be run as root"

# ---------------------------------------------------------------------------
# SECTION 5: Tool check
# ---------------------------------------------------------------------------
log "SECTION 5: Tool check"
need podman
need systemctl
need loginctl
need curl

# ---------------------------------------------------------------------------
# SECTION 6: Collect Odoo credentials
# ---------------------------------------------------------------------------
log "SECTION 6: Odoo credentials"

# Accept from environment or prompt interactively
if [ -z "${ODOO_URL:-}" ]; then
    printf 'ODOO_URL (e.g. https://dapla.net): '; read -r ODOO_URL
fi
if [ -z "${ODOO_DB:-}" ]; then
    printf 'ODOO_DB: '; read -r ODOO_DB
fi
if [ -z "${ODOO_USER:-}" ]; then
    printf 'ODOO_USER (email): '; read -r ODOO_USER
fi
if [ -z "${ODOO_API_KEY:-}" ]; then
    printf 'ODOO_API_KEY: '; stty -echo; read -r ODOO_API_KEY; stty echo; echo
fi

[ -n "$ODOO_URL" ]     || die "ODOO_URL is required"
[ -n "$ODOO_DB" ]      || die "ODOO_DB is required"
[ -n "$ODOO_USER" ]    || die "ODOO_USER is required"
[ -n "$ODOO_API_KEY" ] || die "ODOO_API_KEY is required"

# ---------------------------------------------------------------------------
# SECTION 7: Service account
# ---------------------------------------------------------------------------
log "SECTION 7: Service account"

if ! id "$MCP_USER" >/dev/null 2>&1; then
    useradd --system --uid "$MCP_UID" \
            --create-home --shell /sbin/nologin \
            --comment "odoo-mcp-server service account" \
            "$MCP_USER"
    log "Created user $MCP_USER (uid $MCP_UID)"
else
    log "User $MCP_USER already exists — skipping creation"
fi

MCP_HOME="$(getent passwd "$MCP_USER" | cut -d: -f6)"
MCP_RUNTIME_UID="$(id -u "$MCP_USER")"

# ---------------------------------------------------------------------------
# SECTION 8: Linger
# ---------------------------------------------------------------------------
log "SECTION 8: Linger"

loginctl enable-linger "$MCP_USER"
loginctl show-user "$MCP_USER" 2>/dev/null | grep -q "Linger=yes" \
    || die "Linger not active for $MCP_USER — check systemd-logind"

# ---------------------------------------------------------------------------
# SECTION 9: User manager startup
# ---------------------------------------------------------------------------
log "SECTION 9: User manager"

systemctl start "user@${MCP_RUNTIME_UID}.service" \
    || die "Failed to start user@${MCP_RUNTIME_UID}.service"

_elapsed=0
until [ -S "/run/user/${MCP_RUNTIME_UID}/bus" ]; do
    [ "$_elapsed" -ge "$BUS_TIMEOUT" ] \
        && die "Timed out waiting for D-Bus socket (${BUS_TIMEOUT}s)"
    sleep 1
    _elapsed=$((_elapsed + 1))
done
log "Session bus ready (${_elapsed}s)"

# ---------------------------------------------------------------------------
# SECTION 10: Image pull
# ---------------------------------------------------------------------------
log "SECTION 10: Pulling image (rootless store)"
as_mcp "podman pull ${IMAGE}"

# ---------------------------------------------------------------------------
# SECTION 11: Install quadlet unit files + env
# ---------------------------------------------------------------------------
log "SECTION 11: Installing quadlet unit files"

as_mcp "mkdir -p ${QUADLET_DIR}"

_install_unit() {
    _src="$1"
    _dst="${QUADLET_DIR}/$(basename "$_src")"
    install -m 0644 -o "$MCP_USER" "$_src" "$_dst"
    printf '    wrote %s\n' "$_dst"
}

_install_unit "${SCRIPT_DIR}/containers/odoo-mcp.network"
_install_unit "${SCRIPT_DIR}/containers/odoo-mcp-data.volume"
_install_unit "${SCRIPT_DIR}/containers/odoo-mcp.container"

# Write env file — preserve operator edits on re-runs (idempotent)
_env_dst="${QUADLET_DIR}/odoo-mcp.env"
if [ ! -f "$_env_dst" ]; then
    install -m 0600 -o "$MCP_USER" /dev/null "$_env_dst"
    cat > "$_env_dst" << ENVEOF
# odoo-mcp.env — generated by odoo_mcp_setup.sh on $(date -u +%Y-%m-%dT%H:%M:%SZ)
# Edit carefully — this file contains credentials.
# Regenerate by re-running odoo_mcp_setup.sh.

ODOO_URL=${ODOO_URL}
ODOO_DB=${ODOO_DB}
ODOO_USER=${ODOO_USER}
ODOO_API_KEY=${ODOO_API_KEY}

HOST=127.0.0.1
PORT=${MCP_PORT}
CORS_ORIGINS=https://claude.ai
ENVEOF
    chown "${MCP_USER}:" "$_env_dst"
    chmod 0600 "$_env_dst"
    log "Wrote ${_env_dst}"
else
    log "Env file already exists — preserving operator edits (${_env_dst})"
fi

# ---------------------------------------------------------------------------
# SECTION 12: Start service
# ---------------------------------------------------------------------------
log "SECTION 12: Starting service"

as_mcp "XDG_RUNTIME_DIR=/run/user/${MCP_RUNTIME_UID} \
    DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/${MCP_RUNTIME_UID}/bus \
    systemctl --user daemon-reload"

as_mcp "XDG_RUNTIME_DIR=/run/user/${MCP_RUNTIME_UID} \
    DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/${MCP_RUNTIME_UID}/bus \
    systemctl --user enable --now odoo-mcp.service"

# ---------------------------------------------------------------------------
# SECTION 13: Smoke test
# ---------------------------------------------------------------------------
log "SECTION 13: Smoke test"

_elapsed=0
_timeout=60
until curl -sf "http://127.0.0.1:${MCP_PORT}/healthz" >/dev/null 2>&1; do
    [ "$_elapsed" -ge "$_timeout" ] \
        && die "Service did not respond at /healthz within ${_timeout}s"
    sleep 2
    _elapsed=$((_elapsed + 2))
done

log "Health check passed (${_elapsed}s)"
log ""
log "=== odoo-mcp-server running ==="
log "  MCP endpoint : http://127.0.0.1:${MCP_PORT}/mcp"
log "  Health       : http://127.0.0.1:${MCP_PORT}/healthz"
log "  Logs         : journalctl --user -u odoo-mcp.service -f"
log "  (as ${MCP_USER})"
log ""
log "HAProxy frontend: see examples/haproxy-odoo-mcp.cfg"
log "GitHub secrets:   see docs/secrets.md"
