# Dockerfile — odoo-mcp-server
# Multi-stage: builder compiles the C binary, scratch final image ships it.
#
# Build: docker build -t ghcr.io/denzuko/odoo-mcp-server:latest .
# Run:   docker run --env-file odoo-mcp.env -p 127.0.0.1:8000:8000 \
#                   ghcr.io/denzuko/odoo-mcp-server:latest

# ── Stage 1: build ────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update -q && \
    apt-get install -y --no-install-recommends \
        gcc make libssl-dev ca-certificates curl git \
        # kcgi not in debian — build from source
        autoconf automake libtool pkg-config zlib1g-dev && \
    rm -rf /var/lib/apt/lists/*

# Build kcgi from source (no debian package)
RUN git clone --depth=1 https://github.com/kristapsdz/kcgi /tmp/kcgi && \
    cd /tmp/kcgi && \
    ./configure && make && make install && \
    rm -rf /tmp/kcgi

WORKDIR /src
COPY . .

# Vendor nob.h at build time if not present (CI already has it)
RUN [ -f nob.h ] || curl -sLo nob.h \
    https://raw.githubusercontent.com/tsoding/nob.h/main/nob.h

# Compile nob build driver then build the binary
RUN gcc -std=c99 -D_POSIX_C_SOURCE=200809L -o nob nob.c && \
    ./nob

# ── Stage 2: minimal runtime ──────────────────────────────────────────────
FROM gcr.io/distroless/cc-debian12:nonroot AS final

LABEL org.opencontainers.image.title="odoo-mcp-server" \
      org.opencontainers.image.description="MCP server for Odoo 15+ XML-RPC — C99/BCHS" \
      org.opencontainers.image.source="https://github.com/denzuko/odoo-mcp-server" \
      org.opencontainers.image.licenses="BSD-2-Clause" \
      maintainer="denzuko@dapla.net"

COPY --from=builder /src/odoo-mcp-server /odoo-mcp-server
# Copy shared libs required by libtls/kcgi
COPY --from=builder /usr/lib/x86_64-linux-gnu/libssl.so*    /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libcrypto.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/local/lib/libkcgi.so*              /usr/local/lib/
COPY --from=builder /usr/local/lib/libkcgijson.so*          /usr/local/lib/

ENV PORT=8000 \
    HOST=0.0.0.0

EXPOSE 8000

HEALTHCHECK --interval=30s --timeout=10s --start-period=15s --retries=3 \
  CMD ["/odoo-mcp-server", "--healthcheck"]

ENTRYPOINT ["/odoo-mcp-server"]
