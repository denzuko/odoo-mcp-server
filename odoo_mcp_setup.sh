#!/bin/sh
# odoo_mcp_setup.sh — preprod bootstrap for odoo-mcp-server
#
# Environment: Linux + Podman + ZFS (preprod / staging)
# Image pulled from GHCR (signed, built by CI publish job).
# NOT for production — prod runs the native ELF on FreeBSD/NetBSD
# via rc.d (see deploy/freebsd/ or deploy/netbsd/).
#
# Usage (as root):
#   ./odoo_mcp_setup.sh [--tag v1.10.0] [--zpool tank]
#
# Environment overrides:
#   ODOO_URL, ODOO_DB, ODOO_USER, ODOO_API_KEY
#   MCP_USER, MCP_UID, MCP_TAG, MCP_ZPOOL, MCP_ZFS_DATASET
#
# Da Planet Security / denzuko <denzuko@dapla.net>
# BSD 2-Clause License

set -eu

# ---------------------------------------------------------------------------
# SECTION 1: Defaults
# ---------------------------------------------------------------------------
MCP_USER="${MCP_USER:-odoo-mcp}"
MCP_UID="${MCP_UID:-2002}"
MCP_TAG="${MCP_TAG:-latest}"
MCP_ZPOOL="${MCP_ZPOOL:-tank}"
MCP_ZFS_DATASET="${MCP_ZFS_DATASET:-${MCP_ZPOOL}/odoo-mcp}"
MCP_MOUNTPOINT="/srv/odoo-mcp"
QUADLET_DIR="/home/${MCP_USER}/.config/containers/systemd"
IMAGE="ghcr.io/denzuko/odoo-mcp-server:${MCP_TAG}"
BUS_TIMEOUT=30
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ---------------------------------------------------------------------------
# SECTION 2: Helpers
# ---------------------------------------------------------------------------
log()  { printf '[odoo-mcp-setup] %s\n' "$*"; }
die()  { printf '[odoo-mcp-setup] FATAL: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "Required tool not found: $1"; }
as_mcp() { su -s /bin/sh -c "$1" "${MCP_USER}"; }

# ---------------------------------------------------------------------------
# SECTION 3: Argument parsing
# ---------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        --tag)   shift; MCP_TAG="$1";   IMAGE="ghcr.io/denzuko/odoo-mcp-server:${MCP_TAG}" ;;
        --zpool) shift; MCP_ZPOOL="$1"; MCP_ZFS_DATASET="${MCP_ZPOOL}/odoo-mcp" ;;
        *)       die "Unknown argument: $1" ;;
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
need zfs

# ---------------------------------------------------------------------------
# SECTION 6: Collect Odoo credentials
# ---------------------------------------------------------------------------
log "SECTION 6: Odoo credentials"

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

[ -n "${ODOO_URL}" ]     || die "ODOO_URL is required"
[ -n "${ODOO_DB}" ]      || die "ODOO_DB is required"
[ -n "${ODOO_USER}" ]    || die "ODOO_USER is required"
[ -n "${ODOO_API_KEY}" ] || die "ODOO_API_KEY is required"

# ---------------------------------------------------------------------------
# SECTION 7: Service account
# ---------------------------------------------------------------------------
log "SECTION 7: Service account (uid ${MCP_UID})"

if ! id "${MCP_USER}" >/dev/null 2>&1; then
    useradd --system --uid "${MCP_UID}" \
            --create-home --shell /sbin/nologin \
            --comment "odoo-mcp-server preprod service account" \
            "${MCP_USER}"
    log "Created user ${MCP_USER}"
else
    log "User ${MCP_USER} exists — skipping"
fi

MCP_HOME="$(getent passwd "${MCP_USER}" | cut -d: -f6)"
MCP_RUNTIME_UID="$(id -u "${MCP_USER}")"

# ---------------------------------------------------------------------------
# SECTION 8: ZFS dataset
# ---------------------------------------------------------------------------
log "SECTION 8: ZFS dataset ${MCP_ZFS_DATASET}"

if ! zfs list "${MCP_ZFS_DATASET}" >/dev/null 2>&1; then
    zfs create \
        -o mountpoint="${MCP_MOUNTPOINT}" \
        -o compression=lz4 \
        -o atime=off \
        "${MCP_ZFS_DATASET}"
    log "Created ZFS dataset ${MCP_ZFS_DATASET} → ${MCP_MOUNTPOINT}"
else
    log "ZFS dataset ${MCP_ZFS_DATASET} exists — skipping"
fi

chown "${MCP_USER}:" "${MCP_MOUNTPOINT}"
chmod 750 "${MCP_MOUNTPOINT}"
log "ZFS mountpoint: ${MCP_MOUNTPOINT} (owned by ${MCP_USER})"

# ---------------------------------------------------------------------------
# SECTION 9: Linger
# ---------------------------------------------------------------------------
log "SECTION 9: Linger"
loginctl enable-linger "${MCP_USER}"

# ---------------------------------------------------------------------------
# SECTION 10: User manager startup
# ---------------------------------------------------------------------------
log "SECTION 10: User manager"
systemctl start "user@${MCP_RUNTIME_UID}.service" || true

_elapsed=0
until [ -S "/run/user/${MCP_RUNTIME_UID}/bus" ]; do
    [ "${_elapsed}" -ge "${BUS_TIMEOUT}" ] \
        && die "Timed out waiting for D-Bus (${BUS_TIMEOUT}s)"
    sleep 1; _elapsed=$((_elapsed + 1))
done
log "Session bus ready (${_elapsed}s)"

# ---------------------------------------------------------------------------
# SECTION 11: Pull container image from GHCR
# ---------------------------------------------------------------------------
log "SECTION 11: Pull image ${IMAGE}"
as_mcp "podman pull ${IMAGE}"

# ---------------------------------------------------------------------------
# SECTION 12: Install quadlet unit files + env file
# ---------------------------------------------------------------------------
log "SECTION 12: Install quadlet units"
as_mcp "mkdir -p ${QUADLET_DIR}"

_install_unit() {
    install -m 0644 -o "${MCP_USER}" "$1" "${QUADLET_DIR}/$(basename "$1")"
    log "  installed $(basename "$1")"
}

_install_unit "${SCRIPT_DIR}/containers/odoo-mcp.network"
_install_unit "${SCRIPT_DIR}/containers/odoo-mcp-data.volume"
_install_unit "${SCRIPT_DIR}/containers/odoo-mcp.container"

# Write env file (0600, idempotent — preserves operator edits on re-run)
_env_dst="${QUADLET_DIR}/odoo-mcp.env"
if [ ! -f "${_env_dst}" ]; then
    install -m 0600 -o "${MCP_USER}" /dev/null "${_env_dst}"
    cat > "${_env_dst}" << ENVEOF
# odoo-mcp.env — generated by odoo_mcp_setup.sh $(date -u +%Y-%m-%dT%H:%M:%SZ)
# Edit carefully — this file contains credentials.

ODOO_URL=${ODOO_URL}
ODOO_DB=${ODOO_DB}
ODOO_USER=${ODOO_USER}
ODOO_API_KEY=${ODOO_API_KEY}

HOST=127.0.0.1
PORT=8000
CORS_ORIGINS=https://claude.ai
ENVEOF
    chown "${MCP_USER}:" "${_env_dst}"
    chmod 0600 "${_env_dst}"
    log "Wrote ${_env_dst}"
else
    log "Env file exists — preserving operator edits"
fi

# ---------------------------------------------------------------------------
# SECTION 13: Start service
# ---------------------------------------------------------------------------
log "SECTION 13: Start service"

_run_as_mcp() {
    env XDG_RUNTIME_DIR="/run/user/${MCP_RUNTIME_UID}" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${MCP_RUNTIME_UID}/bus" \
        su -s /bin/sh -c "$1" "${MCP_USER}"
}

_run_as_mcp "systemctl --user daemon-reload"
_run_as_mcp "systemctl --user enable --now odoo-mcp.service"

# ---------------------------------------------------------------------------
# SECTION 14: Smoke test
# ---------------------------------------------------------------------------
log "SECTION 14: Smoke test"
_elapsed=0
until curl -sf "http://127.0.0.1:8000/healthz" >/dev/null 2>&1; do
    [ "${_elapsed}" -ge 60 ] && die "/healthz did not respond within 60s"
    sleep 2; _elapsed=$((_elapsed + 2))
done

log ""
log "=== odoo-mcp-server preprod running ==="
log "  Image      : ${IMAGE}"
log "  ZFS        : ${MCP_ZFS_DATASET} → ${MCP_MOUNTPOINT}"
log "  MCP        : http://127.0.0.1:8000/mcp"
log "  Health     : http://127.0.0.1:8000/healthz"
log "  Logs       : journalctl --user -u odoo-mcp.service -f  (as ${MCP_USER})"
log ""
log "NOTE: This is preprod (Linux/Podman/quadlet)."
log "      Prod target is FreeBSD/NetBSD — see deploy/freebsd/"
