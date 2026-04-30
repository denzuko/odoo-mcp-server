/*
 * nob.c — build driver for odoo-mcp-server
 *
 * Bootstrap:
 *   cc -o nob nob.c && ./nob
 *
 * Targets:
 *   ./nob           — native ELF (FreeBSD / Linux)
 *   ./nob wasm      — wasm32-wasi module for Cloudflare Workers
 *   ./nob clean     — remove build artefacts
 *
 * Native requires: kcgi, kcgijson, libtls
 *   FreeBSD: pkg install kcgi libressl
 *   Linux:   apt install libkcgi-dev libtls-dev (or build from source)
 *
 * WASM requires: wasi-sdk (set WASI_SDK env var or /opt/wasi-sdk default)
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */

/* nob.h uses clock_gettime/nanosleep/timespec — require POSIX.1-2008 */
#define _POSIX_C_SOURCE 200809L
#define NOB_IMPLEMENTATION
#include "nob.h"

#include <string.h>
#include <stdlib.h>

/* ── Build targets ──────────────────────────────────────────────────────── */

#define TARGET_NATIVE "odoo-mcp-server"
#define TARGET_WASM   "odoo-mcp-server.wasm"

/* ── Native build ───────────────────────────────────────────────────────── */

static bool build_native(void)
{
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd,
        "cc", "-std=c99", "-O2",
        "-Wall", "-Wextra", "-Wpedantic",
        "-Wno-unused-parameter",
        "-D_FORTIFY_SOURCE=2",
        "-fstack-protector-strong",
        "-o", TARGET_NATIVE,
        "main.c", "mcp.c", "odoo.c", "net.c", "json.c",
        "-lkcgi", "-lkcgijson", "-ltls");

    nob_log(NOB_INFO, "Building native: %s", TARGET_NATIVE);
    return nob_cmd_run(&cmd);
}

/* ── WASM build ─────────────────────────────────────────────────────────── */

static bool build_wasm(void)
{
    const char *sdk = getenv("WASI_SDK");
    if (NULL == sdk || '\0' == sdk[0]) sdk = "/opt/wasi-sdk";

    /* Build path strings using nob_temp_sprintf */
    const char *clang  = nob_temp_sprintf("%s/bin/clang", sdk);
    const char *sysroot_flag = nob_temp_sprintf(
        "--sysroot=%s/share/wasi-sysroot", sdk);

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd,
        clang,
        "--target=wasm32-wasi",
        sysroot_flag,
        "-std=c99", "-O2",
        "-Wall", "-Wextra",
        "-D__wasm__",
        "-mexec-model=reactor",
        "-o", TARGET_WASM,
        /* WASM: main.c excluded (kcgi HTTP layer, native only)
         * net.c excluded — net.h #ifdef __wasm__ handles transport */
        "mcp.c", "odoo.c", "json.c",
        "-Wl,--export=mcp_handle",
        "-Wl,--no-entry");

    nob_log(NOB_INFO, "Building WASM: %s (sdk=%s)", TARGET_WASM, sdk);
    return nob_cmd_run(&cmd);
}

/* ── Clean ──────────────────────────────────────────────────────────────── */

static void do_clean(void)
{
    nob_log(NOB_INFO, "clean");
    remove(TARGET_NATIVE);
    remove(TARGET_WASM);
}

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (argc > 1 && 0 == strcmp(argv[1], "wasm"))  return build_wasm()  ? 0 : 1;
    if (argc > 1 && 0 == strcmp(argv[1], "clean")) { do_clean(); return 0; }

    return build_native() ? 0 : 1;
}
