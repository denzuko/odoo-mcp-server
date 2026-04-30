/*
 * nob.c — build driver for odoo-mcp-server
 *
 * Bootstrap:
 *   cc nob.c -o nob            # native target
 *   cc -Dwasm nob.c -o nob     # wasm32-wasi target
 *
 * Run:
 *   ./nob                      # build TARGET
 *   ./nob clean                # remove build artefacts
 *
 * All target-specific definitions live in config.h:
 *   TARGET, CC_INPUTS, CC_EXTRA, LINK_FLAGS, LINK_LIBS
 *   BUILD_FOLDER, SRC_FOLDER
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */

#define _POSIX_C_SOURCE 200809L   /* nob.h needs clock_gettime/timespec */
#define NOB_IMPLEMENTATION
#include "nob.h"
#include "config.h"

#include <string.h>

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

    if (argc > 1 && 0 == strcmp(argv[1], "clean")) {
        do_clean();
        return 0;
    }

    return build() ? 0 : 1;
}
