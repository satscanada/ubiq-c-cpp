# Real-world install test: pulls the RPM from the GitHub release,
# installs it, then compiles and runs fpe_test.c against the installed SDK.
#
# Usage: see run_test.sh
FROM rockylinux:9

ARG RELEASE_TAG=v2.2.4-rhel9-fix1
ARG GITHUB_REPO=satscanada/ubiq-c-cpp

# ── patch all base packages first ────────────────────────────────────────────
RUN dnf update -y && \
    dnf install -y curl jq gcc openssl-devel libcurl-devel gmp-devel libunistring-devel && \
    dnf clean all && \
    rm -rf /var/cache/dnf

# ── download and install RPMs from GitHub release ────────────────────────────
RUN echo "Fetching release: ${RELEASE_TAG} from ${GITHUB_REPO}" && \
    mkdir -p /tmp/rpms && cd /tmp/rpms && \
    curl -fsSL \
        "https://api.github.com/repos/${GITHUB_REPO}/releases/tags/${RELEASE_TAG}" \
        | jq -r '.assets[] | select(.name | endswith(".rpm")) | .browser_download_url' \
        | xargs -I{} curl -fsSLO {} && \
    ls -lh /tmp/rpms/*.rpm && \
    dnf install -y /tmp/rpms/*.rpm && \
    rm -rf /tmp/rpms

# ── compile fpe_test against the installed SDK ────────────────────────────────
RUN mkdir -p /app
COPY fpe_test.c /app/fpe_test.c
RUN gcc /app/fpe_test.c \
        -I/usr/local/include \
        -L/usr/local/lib \
        -lubiqclient -lssl -lcrypto -lcurl -lgmp -lunistring \
        -Wno-deprecated-declarations \
        -o /app/fpe_test

# ── run ───────────────────────────────────────────────────────────────────────
# ~/.ubiq/credentials is mounted at runtime via run_test.sh
ENTRYPOINT ["/app/fpe_test"]
