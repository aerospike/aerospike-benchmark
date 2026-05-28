# syntax=docker/dockerfile:1
#
# Aerospike asbench - Docker image.
#
# https://github.com/aerospike/aerospike-benchmark
#
# Supports linux/amd64 and linux/arm64.
# Driven by docker/docker-build.sh, which passes the version-specific URLs +
# SHA256s as build args (and optionally an in-context .deb via ASBENCH_LOCAL_PKG*).
# Build directly only if you set those args yourself; otherwise use the script.
#
# Base: debian:trixie-slim. The .deb is built on Ubuntu 24.04; trixie ships a
# recent glibc and common C libs the binary may dynamically link to — apt
# installs the .deb and anything it (transitively) needs.
#

ARG ASBENCH_AMD64_URL=""
ARG ASBENCH_AMD64_SHA256=""
ARG ASBENCH_ARM64_URL=""
ARG ASBENCH_ARM64_SHA256=""

FROM debian:trixie-slim@sha256:b6e2a152f22a40ff69d92cb397223c906017e1391a73c952b588e51af8883bf8

ARG ASBENCH_AMD64_URL
ARG ASBENCH_AMD64_SHA256
ARG ASBENCH_ARM64_URL
ARG ASBENCH_ARM64_SHA256
ARG ASBENCH_LOCAL_PKG=""
ARG ASBENCH_LOCAL_PKG_AMD64=""
ARG ASBENCH_LOCAL_PKG_ARM64=""

LABEL org.opencontainers.image.title="Aerospike asbench" \
      org.opencontainers.image.description="asbench, a benchmarking tool for Aerospike database." \
      org.opencontainers.image.source="https://github.com/aerospike/aerospike-benchmark" \
      org.opencontainers.image.base.name="docker.io/library/debian:trixie-slim" \
      org.opencontainers.image.licenses="Apache-2.0" \
      org.opencontainers.image.vendor="Aerospike"

SHELL ["/bin/bash", "-Eeuo", "pipefail", "-c"]

# hadolint ignore=DL3008
RUN --mount=type=bind,source=.,target=/build-ctx \
    export DEBIAN_FRONTEND=noninteractive && \
    ARCH="$(dpkg --print-architecture)" && \
    if [ "${ARCH}" = "amd64" ]; then \
        PKG_URL="${ASBENCH_AMD64_URL}"; PKG_SHA="${ASBENCH_AMD64_SHA256}"; \
        LOCAL_PKG="${ASBENCH_LOCAL_PKG_AMD64:-${ASBENCH_LOCAL_PKG}}"; \
    else \
        PKG_URL="${ASBENCH_ARM64_URL}"; PKG_SHA="${ASBENCH_ARM64_SHA256}"; \
        LOCAL_PKG="${ASBENCH_LOCAL_PKG_ARM64:-${ASBENCH_LOCAL_PKG}}"; \
    fi && \
    apt-get update -y && \
    if [ -n "${LOCAL_PKG}" ] && [ -f "/build-ctx/${LOCAL_PKG}" ]; then \
        cp "/build-ctx/${LOCAL_PKG}" /tmp/asbench.deb; \
    else \
        apt-get install -y --no-install-recommends curl ca-certificates && \
        curl -fsSL --retry 3 --retry-delay 3 "${PKG_URL}" -o /tmp/asbench.deb && \
        apt-get purge -y --auto-remove curl; \
    fi && \
    if [ -n "${PKG_SHA}" ] && [ "${PKG_SHA}" != "PLACEHOLDER" ]; then \
        echo "${PKG_SHA}  /tmp/asbench.deb" | sha256sum -c -; \
    fi && \
    apt-get install -y --no-install-recommends /tmp/asbench.deb && \
    rm -f /tmp/asbench.deb && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

CMD ["asbench"]
