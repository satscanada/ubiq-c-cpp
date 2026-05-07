# Rocky Linux 9 — mirrors the GitHub Actions CI environment
# Builds the Ubiq C SDK from source and compiles fpe_test.c for local testing.
#
# Usage: see run_test.sh
FROM rockylinux:9

# ── system deps ──────────────────────────────────────────────────────────────
RUN dnf install -y git && \
    dnf install -y dnf-plugins-core && \
    dnf config-manager --set-enabled crb && \
    dnf install -y \
        cmake gcc gcc-c++ make \
        openssl-devel libcurl-devel \
        gmp-devel libunistring-devel \
        boost-devel && \
    dnf clean all

# ── copy SDK source ──────────────────────────────────────────────────────────
WORKDIR /sdk
COPY . .

# ── verify HTTP/2 fix is present ─────────────────────────────────────────────
RUN grep -n "HTTP_VERSION" src/lib/curl.c || \
    (echo "ERROR: HTTP/2 fix not found in curl.c" && exit 1)

# ── build SDK (tests off, static libs only) ──────────────────────────────────
RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_C_FLAGS="-Wno-implicit-function-declaration" \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    make -j1 && \
    make install

# ── build fpe_test ────────────────────────────────────────────────────────────
COPY docker-test/fpe_test.c /app/fpe_test.c
RUN gcc /app/fpe_test.c \
        -I/usr/local/include \
        -L/usr/local/lib \
        -lubiqclient -lssl -lcrypto -lcurl -lgmp -lunistring \
        -o /app/fpe_test

# ── run ───────────────────────────────────────────────────────────────────────
# ~/.ubiq/credentials is mounted at runtime via run_test.sh
ENTRYPOINT ["/app/fpe_test"]
