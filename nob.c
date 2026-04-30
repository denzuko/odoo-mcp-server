/*
 * nob.c — build driver for odoo-mcp-server
 *
 * Bootstrap:
 *   cc -o nob nob.c            # native ELF target
 *   cc -Dwasm -o nob nob.c     # wasm32-wasi target
 *
 * Then run:
 *   ./nob                      # build
 *   ./nob clean                # remove artefacts
 *
 * Native requires: cc, kcgi, kcgijson, libtls
 *   FreeBSD: pkg install kcgi libressl
 *
 * WASM requires: wasi-sdk (WASI_SDK env var, default /opt/wasi-sdk)
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */

#define _POSIX_C_SOURCE 200809L   /* nob.h needs clock_gettime/timespec */
#define NOB_IMPLEMENTATION
#include "nob.h"

#include <stdlib.h>
#include <string.h>

/* ── Targets ────────────────────────────────────────────────────────────── */

#ifdef wasm
#  define TARGET "odoo-mcp-server.wasm"
#else
#  define TARGET "odoo-mcp-server"
#endif

/* ── Native build ───────────────────────────────────────────────────────── */

#ifndef wasm
static bool build_native(void)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd,
        "cc", "-std=c99", "-O2",
        "-Wall", "-Wextra", "-Wpedantic",
        "-Wno-unused-parameter",
        "-D_FORTIFY_SOURCE=2",
        "-fstack-protector-strong",
        "-o", TARGET,
        "main.c", "mcp.c", "odoo.c", "net.c",
        "-lkcgi", "-lkcgijson", "-ltls");
    nob_log(NOB_INFO, "building native: %s", TARGET);
    return nob_cmd_run(&cmd);
}
#endif

/* ── WASM build ─────────────────────────────────────────────────────────── */

#ifdef wasm
static bool build_wasm(void)
{
    const char *sdk = getenv("WASI_SDK");
    if (NULL == sdk || '\0' == sdk[0]) sdk = "/opt/wasi-sdk";

    const char *clang        = nob_temp_sprintf("%s/bin/clang", sdk);
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
        "-o", TARGET,
        /* main.c: kcgi HTTP layer, native only — excluded
         * net.c:  BSD sockets, native only — #ifdef __wasm__ in net.h */
        "mcp.c", "odoo.c",
        "-Wl,--export=mcp_handle",
        "-Wl,--no-entry");
    nob_log(NOB_INFO, "building wasm: %s (sdk=%s)", TARGET, sdk);
    return nob_cmd_run(&cmd);
}
#endif

/* ── Clean ──────────────────────────────────────────────────────────────── */

static void do_clean(void)
{
    nob_log(NOB_INFO, "clean");
    remove("odoo-mcp-server");
    remove("odoo-mcp-server.wasm");
    remove("nob");
}

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (argc > 1 && 0 == strcmp(argv[1], "clean")) { do_clean(); return 0; }

#ifdef wasm
    return build_wasm()  ? 0 : 1;
#else
    return build_native() ? 0 : 1;
#endif
}
