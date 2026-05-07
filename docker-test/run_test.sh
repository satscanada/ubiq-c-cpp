#!/usr/bin/env bash
# run_test.sh — Build and run the fpe_test in a Rocky Linux 9 Docker container.
#
# Prerequisites:
#   - Docker installed and running
#   - ~/.ubiq/credentials file with valid [default] credentials
#
# Usage:
#   cd ubiq-c-cpp          # repo root
#   bash docker-test/run_test.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE_NAME="ubiq-fpe-test:latest"
CREDS_FILE="${HOME}/.ubiq/credentials"

# Release tag to pull RPMs from — override with: RELEASE_TAG=vX.Y.Z bash run_test.sh
RELEASE_TAG="${RELEASE_TAG:-v2.2.4-rhel9-fix1}"
GITHUB_REPO="satscanada/ubiq-c-cpp"

# ── pre-flight checks ─────────────────────────────────────────────────────────
if ! command -v docker &>/dev/null; then
    echo "ERROR: Docker is not installed or not in PATH."
    exit 1
fi

if [[ ! -f "${CREDS_FILE}" ]]; then
    echo "ERROR: Credentials file not found at ${CREDS_FILE}"
    echo ""
    echo "Create it with:"
    echo "  mkdir -p ~/.ubiq"
    echo "  cat > ~/.ubiq/credentials << 'EOF'"
    echo "  [default]"
    echo "  ACCESS_KEY_ID = your_access_key_id"
    echo "  SECRET_SIGNING_KEY = your_secret_signing_key"
    echo "  SECRET_CRYPTO_ACCESS_KEY = your_secret_crypto_access_key"
    echo "  EOF"
    echo "  chmod 600 ~/.ubiq/credentials"
    exit 1
fi

# ── build image ───────────────────────────────────────────────────────────────
echo "============================================================"
echo " Building Docker image: ${IMAGE_NAME}"
echo " Base: rockylinux:9 (mirrors GitHub Actions CI)"
echo "============================================================"

# Build context is docker-test/ only — no need to send the full repo
docker build \
    -f "${SCRIPT_DIR}/Dockerfile" \
    --build-arg RELEASE_TAG="${RELEASE_TAG}" \
    --build-arg GITHUB_REPO="${GITHUB_REPO}" \
    -t "${IMAGE_NAME}" \
    "${SCRIPT_DIR}"

echo ""
echo "============================================================"
echo " Running FPE encrypt/decrypt test"
echo " Credentials: ${CREDS_FILE} (mounted read-only)"
echo "============================================================"
echo ""

# Mount credentials read-only; container runs as root to match CI
docker run --rm \
    -v "${CREDS_FILE}:/root/.ubiq/credentials:ro" \
    "${IMAGE_NAME}"

echo ""
echo "Done."
