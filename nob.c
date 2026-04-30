/*
 * nob.c — COMPILE-TIME BUILD DRIVER for odoo-mcp-server
 *
 * This is a build driver only — it compiles source into binaries.
 * It is NOT a task runner, DAG, or deployment tool.
 * Deployment is handled by:
 *   CF Workers  — terraform/ (pulls .wasm from GitHub Release)
 *   Preprod     — odoo_mcp_setup.sh (pulls image from GHCR)
 *   Prod FreeBSD — deploy/freebsd/odoo_mcp_deploy.sh (pulls ELF from GitHub Release)
 *   Prod NetBSD  — deploy/netbsd/odoo_mcp_deploy.sh
 *
 * Bootstrap:
 *   cc nob.c -o nob            # native target
 *   cc -Dwasm nob.c -o nob     # wasm32-wasi target
 *
 * Run:
 *   ./nob                      # build → build/TARGET
 *   ./nob clean                # remove build/ artefacts
 *
 * All target-specific definitions live in config.h.
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */

#define _POSIX_C_SOURCE 200809L   /* nob.h needs clock_gettime/timespec */
#define NOB_IMPLEMENTATION
#include "nob.h"
#include "config.h"

#include <string.h>

static bool build_test(void)
{
    nob_mkdir_if_not_exists(BUILD_FOLDER);

    /* Compile impl.c — owns ARENA_IMPLEMENTATION, RC_IMPLEMENTATION, SJ_IMPL */
    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cmd_append(&cmd, "-std=c99", "-O2", "-Wno-unused-parameter", "-I.");
    nob_cc_output(&cmd, BUILD_FOLDER "impl.o");
    nob_cmd_append(&cmd, "-c", "impl.c");
    nob_log(NOB_INFO, "compiling impl.c");
    if (!nob_cmd_run(&cmd)) return false;

    /* Compile mcp.c */
    cmd = (Nob_Cmd){0};
    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cmd_append(&cmd, "-std=c99", "-O2", "-Wno-unused-parameter", "-I.");
    nob_cc_output(&cmd, BUILD_FOLDER "mcp.o");
    nob_cmd_append(&cmd, "-c", "mcp.c");
    nob_log(NOB_INFO, "compiling mcp.c");
    if (!nob_cmd_run(&cmd)) return false;

    /* Compile odoo.c */
    cmd = (Nob_Cmd){0};
    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cmd_append(&cmd, "-std=c99", "-O2", "-Wno-unused-parameter", "-I.");
    nob_cc_output(&cmd, BUILD_FOLDER "odoo.o");
    nob_cmd_append(&cmd, "-c", "odoo.c");
    nob_log(NOB_INFO, "compiling odoo.c");
    if (!nob_cmd_run(&cmd)) return false;

    /* Link tests binary — stubs net_http_post, no kcgi */
    cmd = (Nob_Cmd){0};
    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cmd_append(&cmd, "-std=c99", "-O2", "-Wno-unused-parameter", "-I.");
    nob_cc_output(&cmd, BUILD_FOLDER "tests");
    nob_cmd_append(&cmd, "tests.c",
                   BUILD_FOLDER "impl.o",
                   BUILD_FOLDER "mcp.o",
                   BUILD_FOLDER "odoo.o");
    nob_log(NOB_INFO, "linking tests");
    if (!nob_cmd_run(&cmd)) return false;

    cmd = (Nob_Cmd){0};
    nob_cmd_append(&cmd, BUILD_FOLDER "tests");
    nob_log(NOB_INFO, "running tests");
    return nob_cmd_run(&cmd);
}

static bool build(void)
{
    nob_mkdir_if_not_exists(BUILD_FOLDER);

    Nob_Cmd cmd = {0};

    /* Compiler */
    nob_cc(&cmd);

    /* Standard warning flags */
    nob_cc_flags(&cmd);

    /* Optimisation + hardening flags common to both targets */
    nob_cmd_append(&cmd, "-std=c99", "-O2");

    /* Target-specific extra flags (CC_EXTRA from config.h) */
    nob_cmd_append(&cmd, CC_EXTRA);

    /* Output binary */
    nob_cc_output(&cmd, BUILD_FOLDER TARGET);

    /* Source files (CC_INPUTS from config.h) */
    nob_cc_inputs(&cmd, CC_INPUTS);

    /* Linker flags, then libraries (LINK_FLAGS / LINK_LIBS from config.h) */
    nob_cmd_append(&cmd, LINK_FLAGS);
    nob_cmd_append(&cmd, LINK_LIBS);

    nob_log(NOB_INFO, "building %s", BUILD_FOLDER TARGET);
    return nob_cmd_run(&cmd);
}

static void do_clean(void)
{
    nob_log(NOB_INFO, "clean");
    remove(BUILD_FOLDER TARGET);
    /* Remove the build folder itself only when empty */
    rmdir(BUILD_FOLDER);
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (argc > 1 && 0 == strcmp(argv[1], "test"))  return build_test()  ? 0 : 1;
    if (argc > 1 && 0 == strcmp(argv[1], "clean")) { do_clean(); return 0; }

    return build() ? 0 : 1;
}
