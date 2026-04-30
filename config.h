/*
 * config.h — build configuration (nob.c) + runtime env binding
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>
#include <stdio.h>

/* ── Build configuration (consumed by nob.c) ────────────────────────────── */

#define BUILD_FOLDER "build/"
#define SRC_FOLDER   ""          /* sources at project root */

#ifdef __wasm__
#  define TARGET     "odoo-mcp-server.wasm"
#  define CC_INPUTS  "mcp.c", "odoo.c"
#  define CC_EXTRA   "--target=wasm32-wasi", "-D__wasm__", \
                     "-mexec-model=reactor"
#  define LINK_FLAGS "-Wl,--export=mcp_handle", "-Wl,--no-entry"
#  define LINK_LIBS  /* none */
#else
#  define TARGET     "odoo-mcp-server"
#  define CC_INPUTS  "main.c", "mcp.c", "odoo.c", "net.c"
#  define CC_EXTRA   "-D_FORTIFY_SOURCE=2", "-fstack-protector-strong", \
                     "-Wpedantic", "-Wno-unused-parameter"
#  define LINK_FLAGS /* none */
#  define LINK_LIBS  "-lkcgi", "-lkcgijson", "-ltls"
#endif

/* ── Runtime configuration ───────────────────────────────────────────────── */

/* Server listen defaults (native target only) */
#define CFG_DEFAULT_PORT  "8000"
#define CFG_DEFAULT_HOST  "127.0.0.1"

/* MCP protocol version we advertise */
#define MCP_PROTO_VERSION "2025-03-26"
#define MCP_SERVER_NAME   "odoo-mcp-server"
#define MCP_SERVER_VER    "1.0.0"

typedef struct {
    const char *odoo_url;     /* ODOO_URL  — e.g. https://dapla.net  */
    const char *odoo_db;      /* ODOO_DB   — database name            */
    const char *odoo_user;    /* ODOO_USER — login email              */
    const char *odoo_apikey;  /* ODOO_API_KEY                         */
    const char *host;         /* HOST      — bind address             */
    const char *port;         /* PORT      — listen port              */
} Config;

/*
 * config_load — read env vars, die loudly on missing required values.
 * Returns populated Config. Caller owns nothing (pointers into environ).
 */
static inline Config config_load(void)
{
    Config c = {0};

#define REQUIRE(field, var)                                         \
    do {                                                            \
        c.field = getenv(var);                                      \
        if (NULL == c.field || '\0' == c.field[0]) {               \
            fprintf(stderr, "fatal: %s is not set\n", var);        \
            exit(1);                                                \
        }                                                           \
    } while (0)

#define OPTIONAL(field, var, def)                                   \
    do {                                                            \
        c.field = getenv(var);                                      \
        if (NULL == c.field || '\0' == c.field[0]) c.field = def;  \
    } while (0)

    REQUIRE (odoo_url,    "ODOO_URL");
    REQUIRE (odoo_db,     "ODOO_DB");
    REQUIRE (odoo_user,   "ODOO_USER");
    REQUIRE (odoo_apikey, "ODOO_API_KEY");
    OPTIONAL(host,        "HOST", CFG_DEFAULT_HOST);
    OPTIONAL(port,        "PORT", CFG_DEFAULT_PORT);

#undef REQUIRE
#undef OPTIONAL

    return c;
}

#endif /* CONFIG_H */
