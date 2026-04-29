/*
 * nob.c — build driver for odoo-mcp-server
 *
 * Usage:
 *   cc -o nob nob.c && ./nob          # native ELF (FreeBSD/Linux)
 *   ./nob wasm                         # WASM module for CF Workers
 *   ./nob clean                        # remove build artifacts
 *
 * Requires for native: kcgi, kcgijson, libtls  (pkg install kcgi libressl)
 * Requires for wasm:   wasi-sdk  (set WASI_SDK env var or /opt/wasi-sdk)
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * MIT License
 */

#define NOB_IMPLEMENTATION
#include "nob.h"

#include <string.h>
#include <stdlib.h>

/* ── Build targets ─────────────────────────────────────────────────────── */

#define TARGET_NATIVE "odoo-mcp"
#define TARGET_WASM   "odoo-mcp.wasm"

/* Source files shared by both targets (net.c excluded from WASM via #ifdef) */
#define SRCS_COMMON  "main.c", "mcp.c", "odoo.c", "json.c"
#define SRCS_NATIVE  SRCS_COMMON, "net.c"
/* WASM: net.c excluded — __wasm__ branch in net.h handles transport */
#define SRCS_WASM    "mcp.c", "odoo.c", "json.c"

/* ── Native build ──────────────────────────────────────────────────────── */

static bool build_native(void)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd,
        "cc",
        "-std=c99",
        "-O2",
        "-Wall", "-Wextra", "-Wpedantic",
        "-Wno-unused-parameter",
        /* Hardening */
        "-D_FORTIFY_SOURCE=2",
        "-fstack-protector-strong",
        "-o", TARGET_NATIVE,
        /* Sources */
        SRCS_NATIVE,
        /* Libraries */
        "-lkcgi", "-lkcgijson", "-ltls",
        NULL);

    nob_log(NOB_INFO, "Building native: %s", TARGET_NATIVE);
    return nob_cmd_run_sync(cmd);
}

/* ── WASM build ────────────────────────────────────────────────────────── */

static bool build_wasm(void)
{
    /* Locate wasi-sdk */
    const char *sdk = getenv("WASI_SDK");
    if (NULL == sdk || '\0' == *sdk) sdk = "/opt/wasi-sdk";

    char clang[512];
    char sysroot[512];
    snprintf(clang,   sizeof clang,   "%s/bin/clang", sdk);
    snprintf(sysroot, sizeof sysroot, "%s/share/wasi-sysroot", sdk);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd,
        clang,
        "--target=wasm32-wasi",
        "-std=c99",
        "-O2",
        "-Wall", "-Wextra",
        /* Tell our code it's the WASM target */
        "-D__wasm__",
        /* WASM-specific: no start function, reactor model */
        "-mexec-model=reactor",
        /* sysroot */
        NOB_CONCAT("--sysroot=", sysroot),
        "-o", TARGET_WASM,
        /* Sources — no main.c (CF Worker JS shim is the entry), no net.c */
        SRCS_WASM,
        /* Export the MCP handler for the JS shim */
        "-Wl,--export=mcp_handle",
        "-Wl,--export=arena_new",
        "-Wl,--export=arena_reset",
        "-Wl,--export=arena_free",
        "-Wl,--no-entry",
        NULL);

    nob_log(NOB_INFO, "Building WASM: %s (sdk=%s)", TARGET_WASM, sdk);
    return nob_cmd_run_sync(cmd);
}

/* ── Clean ─────────────────────────────────────────────────────────────── */

static void do_clean(void)
{
    nob_log(NOB_INFO, "clean");
    remove(TARGET_NATIVE);
    remove(TARGET_WASM);
    remove("nob");
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (argc > 1 && 0 == strcmp(argv[1], "wasm"))  return build_wasm()  ? 0 : 1;
    if (argc > 1 && 0 == strcmp(argv[1], "clean")) { do_clean(); return 0; }

    return build_native() ? 0 : 1;
}
