# ── Stage 1: build ───────────────────────────────────────────────────────────
# Rocky Linux 9 — mirrors the GitHub Actions CI environment.
# All packages are updated to latest to patch known CVEs before building.
FROM rockylinux:9 AS builder

# Apply all security patches first
RUN dnf update -y && \
    dnf install -y git && \
    dnf install -y dnf-plugins-core && \
    dnf config-manager --set-enabled crb && \
    dnf install -y \
        cmake gcc gcc-c++ make \
        openssl-devel libcurl-devel \
        gmp-devel libunistring-devel \
        boost-devel && \
    dnf clean all && \
    rm -rf /var/cache/dnf

# Copy SDK source
WORKDIR /sdk
COPY . .

# Verify HTTP/2 fix is present
RUN grep -n "HTTP_VERSION" src/lib/curl.c || \
    (echo "ERROR: HTTP/2 fix not found in curl.c" && exit 1)

# Build SDK (tests off, static libs only)
RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_C_FLAGS="-Wno-implicit-function-declaration" \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    make -j1 && \
    make install

# Build fpe_test — statically linked so it runs in a minimal final image
RUN gcc /sdk/docker-test/fpe_test.c \
        -I/usr/local/include \
        -L/usr/local/lib \
        -static \
        -lubiqclient -lssl -lcrypto -lcurl -lgmp -lunistring \
        -lpthread -ldl -lz \
        -o /app/fpe_test

# ── Stage 2: runtime ──────────────────────────────────────────────────────────
# Minimal updated base — only the patched OS + the statically linked binary.
# No compilers, no headers, no SDK source — drastically reduces attack surface.
FROM rockylinux:9-minimal AS runtime

RUN microdnf update -y && \
    microdnf clean all && \
    rm -rf /var/cache/dnf

COPY --from=builder /app/fpe_test /app/fpe_test

# ~/.ubiq/credentials is mounted at runtime via run_test.sh
ENTRYPOINT ["/app/fpe_test"]
