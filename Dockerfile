# syntax=docker/dockerfile:1

# =============================================================================
# Stage 1: Drogon.
#
# Drogon is built from source because the distro package (like Homebrew's) is
# compiled WITHOUT the PostgreSQL backend. libpq-dev and libhiredis-dev must be
# present at THIS point or the backends are silently excluded.
#
# This stage is its own layer, so editing application code never rebuilds
# Drogon -- that is the difference between a 20-second and a 15-minute build.
# =============================================================================
FROM debian:bookworm-slim AS drogon

ARG DROGON_VERSION=v1.9.13
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates pkg-config \
        libjsoncpp-dev uuid-dev zlib1g-dev libssl-dev \
        libpq-dev libhiredis-dev libbrotli-dev \
    && rm -rf /var/lib/apt/lists/*
# NOTE: libc-ares-dev is deliberately NOT installed -- trantor falls back to
# the OS resolver, which is fine here and keeps the image smaller.
#
# It is NOT the fix for Docker service names. Drogon's createRedisClient()
# feeds its host argument straight to trantor::InetAddress, which parses a
# numeric address only, so "redis" became 0.0.0.0. That is handled in
# src/utils/Net.cpp by resolving the name before the client is created.

RUN git clone --depth 1 --branch ${DROGON_VERSION} \
        https://github.com/drogonframework/drogon.git /tmp/drogon \
    && cd /tmp/drogon \
    && git submodule update --init --recursive --depth 1 \
    && cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_CTL=OFF \
        -DBUILD_ORM=ON \
        -DBUILD_POSTGRESQL=ON \
        -DBUILD_REDIS=ON \
        -DBUILD_MYSQL=OFF \
        -DBUILD_SQLITE=OFF \
    && cmake --build build -j"$(nproc)" \
    && cmake --install build \
    && rm -rf /tmp/drogon

# Fail the build here rather than at runtime if the backends are missing.
RUN ldd /usr/local/lib/libdrogon.so | grep -q libpq \
    && ldd /usr/local/lib/libdrogon.so | grep -q hiredis \
    && echo "OK: drogon linked against libpq and hiredis"

# =============================================================================
# Stage 2: application
# =============================================================================
FROM drogon AS builder

WORKDIR /src
# Copy the build definition first: this layer is cached unless CMakeLists or
# the vendored headers change.
COPY CMakeLists.txt ./
COPY third_party/ ./third_party/

RUN apt-get update && apt-get install -y --no-install-recommends \
        libsodium-dev \
    && rm -rf /var/lib/apt/lists/*

COPY src/ ./src/

# Tests need network access to fetch GoogleTest, so they are disabled in the
# image build. Run them in CI, not here.
RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
    && cmake --build build -j"$(nproc)"

# =============================================================================
# Stage 3: runtime.
#
# Ships only the binary and the shared libraries it needs -- no compiler, no
# source, no git. Smaller image, faster pulls, and far less attack surface.
# =============================================================================
FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libjsoncpp25 libssl3 libpq5 libhiredis0.14 \
        libbrotli1 zlib1g libuuid1 libsodium23 \
        curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Drogon's shared libraries, built in stage 1.
COPY --from=drogon /usr/local/lib/libdrogon.so* /usr/local/lib/
COPY --from=drogon /usr/local/lib/libtrantor.so* /usr/local/lib/
RUN ldconfig

# Run as a non-root user: a container process should never be root by default.
RUN useradd --system --create-home --shell /usr/sbin/nologin appuser

COPY --from=builder /src/build/url_shortener /usr/local/bin/url_shortener

# Needed only by standalone (no-nginx) deployments, but small enough to always
# ship: the UI, the API spec, and the SQL the migration runner applies.
COPY --chown=appuser:appuser web/ /app/web/
COPY --chown=appuser:appuser db/  /app/db/

USER appuser
WORKDIR /app

EXPOSE 8080

# Compose also defines a healthcheck; this one makes `docker run` useful too.
HEALTHCHECK --interval=10s --timeout=3s --start-period=15s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8080/health || exit 1

CMD ["/usr/local/bin/url_shortener"]
