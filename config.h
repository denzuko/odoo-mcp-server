/*
 * config.h — environment variable binding
 * All ODOO_* config lives here. Fail fast at startup if missing.
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>
#include <stdio.h>

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
