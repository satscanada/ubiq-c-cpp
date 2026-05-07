# Integrating the Ubiq SDK RPM into a RHEL 9.7 (Plow) C/C++ Application Image

This guide explains how to take the Ubiq C/C++ SDK RPM that is already installed
on a RHEL 9.7 host (or GitHub release) and wire it into the Docker image of an
existing C/C++ application — covering what the RPM puts on disk, which runtime
files your container needs, how to link, and a complete Dockerfile pattern.

---

## 1. What the RPM Installs

After `dnf install ubiqclient-*.rpm` the following paths are populated under
`/usr/local` (the `CMAKE_INSTALL_PREFIX` used when the RPM was built):

| Path | Contents |
|------|----------|
| `/usr/local/include/ubiq/platform.h` | Top-level C header (include this one) |
| `/usr/local/include/ubiq/platform/` | Individual feature headers (`encrypt.h`, `decrypt.h`, `fpe.h`, …) |
| `/usr/local/lib/libubiqclient.a` | Static library (C API) |
| `/usr/local/lib/libubiqclient.so` → `libubiqclient.so.2` | Shared library symlink |
| `/usr/local/lib/libubiqclient.so.2` → `libubiqclient.so.2.2.4` | SONAME symlink |
| `/usr/local/lib/libubiqclient.so.2.2.4` | Versioned shared library |
| `/usr/local/lib/libubiqclient++.a` | Static library (C++ API) |
| `/usr/local/lib/libubiqclient++.so*` | Shared library (C++ API), same versioning |

### Runtime dependencies of the shared library

The `.so` links against these system libraries that must be present in any
container that uses it:

| Library | DNF package |
|---------|-------------|
| `libcurl.so.4` | `libcurl` |
| `libcrypto.so.3` / `libssl.so.3` | `openssl-libs` |
| `libgmp.so.10` | `gmp` |
| `libunistring.so.5` | `libunistring` |
| `libm.so.6` | `glibc` (always present) |

> **Note:** The `-devel` packages (`libcurl-devel`, `openssl-devel`, etc.) are
> only needed at **build time** (for headers). The plain runtime packages above
> are what matters at **run time**.

---

## 2. Inspecting the Downloaded RPM

Before installing, verify the RPM is the correct package and review exactly
what it will put on disk.

### Download the RPM to a local directory (host or container)

```bash
# Example: download from the GitHub release
RELEASE_TAG=v2.2.4-rhel9-fix1
GITHUB_REPO=satscanada/ubiq-c-cpp

mkdir -p ~/ubiq-rpms && cd ~/ubiq-rpms
curl -fsSL "https://api.github.com/repos/${GITHUB_REPO}/releases/tags/${RELEASE_TAG}" \
  | jq -r '.assets[] | select(.name | endswith(".rpm")) | .browser_download_url' \
  | xargs -I{} curl -fsSLO {}

ls -lh ~/ubiq-rpms/
# ubiqclient-2.2.4-1.el9.x86_64.rpm   (runtime + shared libs)
# ubiqclient-devel-2.2.4-1.el9.x86_64.rpm  (headers + static libs)
```

### `rpm -qp` — query package name and version (without installing)

```bash
rpm -qp ~/ubiq-rpms/ubiqclient-*.rpm
```

Expected output:
```
ubiqclient-2.2.4-1.el9.x86_64
ubiqclient-devel-2.2.4-1.el9.x86_64
```

`-q` = query mode, `-p` = operate on a local `.rpm` file rather than the
installed package database.

---

### `rpm -qpi` — full package information (metadata)

```bash
rpm -qpi ~/ubiq-rpms/ubiqclient-2.2.4-1.el9.x86_64.rpm
```

Expected output (excerpt):
```
Name        : ubiqclient
Version     : 2.2.4
Release     : 1.el9
Architecture: x86_64
Install Date: (not installed)
Group       : Unspecified
Size        : <bytes>
License     : unknown
Signature   : (none)
Summary     : ubiqclient
Description :
ubiqclient
```

`-i` = show package info (name, version, architecture, description, license,
size, build date). Useful to confirm you have the right build before installing
it on a production host.

---

### `rpm -ql` — list all files the package will install

For a **downloaded** (not yet installed) RPM use `-p` together with `-l`:

```bash
rpm -qpl ~/ubiq-rpms/ubiqclient-2.2.4-1.el9.x86_64.rpm
```

Expected output:
```
/usr/local/lib/libubiqclient.so.2.2.4
```

```bash
rpm -qpl ~/ubiq-rpms/ubiqclient-devel-2.2.4-1.el9.x86_64.rpm
```

Expected output (excerpt):
```
/usr/local/include/ubiq/platform.h
/usr/local/include/ubiq/platform/decrypt.h
/usr/local/include/ubiq/platform/encrypt.h
/usr/local/include/ubiq/platform/fpe.h
...
/usr/local/lib/libubiqclient.a
/usr/local/lib/libubiqclient.so
/usr/local/lib/libubiqclient.so.2
/usr/local/lib/libubiqclient++.a
/usr/local/lib/libubiqclient++.so
/usr/local/lib/libubiqclient++.so.2
/usr/local/lib/libubiqclient++.so.2.2.4
```

For an **already installed** package (no `-p`):

```bash
rpm -ql ubiqclient
rpm -ql ubiqclient-devel
```

---

### `rpm -q --requires` — show shared library dependencies declared in the RPM

```bash
rpm -qp --requires ~/ubiq-rpms/ubiqclient-2.2.4-1.el9.x86_64.rpm
```

This lists every `lib*.so` the package declares as a dependency, matching the
runtime table in Section 1.

---

### `sudo ldconfig` — register the shared library with the system linker

After installing the RPM the shared library lands in `/usr/local/lib`, which
is **not** in the default `ldconfig` search path on RHEL 9. You must register
it before any dynamically linked binary can find it:

```bash
# Option A: add /usr/local/lib to the ld.so search path (persists across reboots)
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/ubiq-local.conf
sudo ldconfig

# Verify the cache was updated
ldconfig -p | grep ubiqclient
# Expected:
#   libubiqclient.so.2 (libc6,x86-64) => /usr/local/lib/libubiqclient.so.2.2.4
#   libubiqclient++.so.2 (libc6,x86-64) => /usr/local/lib/libubiqclient++.so.2.2.4
```

```bash
# Option B: one-shot for the current shell session only (no root needed)
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

> **In a Dockerfile** run `RUN ldconfig` (no `sudo`) after copying the `.so`
> files — the build runs as root by default.

#### Confirm the binary resolves correctly

```bash
ldd /path/to/your/app | grep ubiq
# libubiqclient.so.2 => /usr/local/lib/libubiqclient.so.2.2.4 (0x...)
```

If you see `not found` instead, `ldconfig` has not been run or `/usr/local/lib`
is not in the cache.

---

## 4. Credentials File

At runtime the SDK reads API credentials from `~/.ubiq/credentials` (INI
format) or from environment variables. The default profile looks like:

```ini
[default]
ACCESS_KEY_ID     = <your-access-key>
SECRET_SIGNING_KEY = <your-secret-key>
SECRET_CRYPTO_ACCESS_KEY = <your-crypto-key>
```

Mount this file **read-only** into the container — never bake credentials into
the image:

```bash
docker run --rm \
  -v "$HOME/.ubiq/credentials:/root/.ubiq/credentials:ro" \
  my-app-image
```

Alternatively pass them as environment variables (check the SDK docs for the
exact variable names).

---

## 5. Linking Your Application

### Static linking (recommended for container images — no `.so` needed at runtime)

```bash
# C
gcc myapp.c \
    -I/usr/local/include \
    -L/usr/local/lib \
    -lubiqclient \
    -lssl -lcrypto -lcurl -lgmp -lunistring \
    -Wno-deprecated-declarations \
    -o myapp

# C++
g++ myapp.cpp \
    -I/usr/local/include \
    -L/usr/local/lib \
    -lubiqclient++ \
    -lssl -lcrypto -lcurl -lgmp -lunistring \
    -Wno-deprecated-declarations \
    -o myapp
```

### Dynamic linking (smaller binary, `.so` must be in the runtime image)

```bash
# C
gcc myapp.c \
    -I/usr/local/include \
    -L/usr/local/lib \
    -lubiqclient \
    -lssl -lcrypto -lcurl -lgmp -lunistring \
    -Wl,-rpath,/usr/local/lib \
    -o myapp
```

`-Wl,-rpath,/usr/local/lib` bakes the library search path into the binary so
`LD_LIBRARY_PATH` is not needed at runtime.  Alternatively add
`/usr/local/lib` to `/etc/ld.so.conf.d/` and run `ldconfig`.

### CMake project

```cmake
find_package(OpenSSL REQUIRED)
find_library(UBIQ_LIB ubiqclient PATHS /usr/local/lib REQUIRED)

target_include_directories(myapp PRIVATE /usr/local/include)
target_link_libraries(myapp
    ${UBIQ_LIB}
    OpenSSL::SSL OpenSSL::Crypto
    curl gmp unistring)
```

---

## 6. Multi-Stage Dockerfile Pattern

Use a **builder stage** (with `-devel` packages) to compile, then copy only
the binary and required `.so` files into a **slim runtime stage**.

```dockerfile
# ── Stage 1: build ────────────────────────────────────────────────────────────
FROM rockylinux:9 AS builder

ARG RELEASE_TAG=v2.2.4-rhel9-fix1
ARG GITHUB_REPO=satscanada/ubiq-c-cpp

# Enable CRB repo (needed for libunistring-devel) and install build deps
RUN dnf install -y dnf-plugins-core && \
    dnf config-manager --set-enabled crb && \
    dnf update -y --allowerasing && \
    dnf install -y --allowerasing \
        curl jq gcc gcc-c++ \
        openssl-devel libcurl-devel gmp-devel libunistring-devel && \
    dnf clean all && rm -rf /var/cache/dnf

# Install the Ubiq SDK RPM from the GitHub release
RUN mkdir -p /tmp/rpms && cd /tmp/rpms && \
    curl -fsSL \
        "https://api.github.com/repos/${GITHUB_REPO}/releases/tags/${RELEASE_TAG}" \
    | jq -r '.assets[] | select(.name | endswith(".rpm")) | .browser_download_url' \
    | xargs -I{} curl -fsSLO {} && \
    dnf install -y /tmp/rpms/*.rpm && \
    rm -rf /tmp/rpms

WORKDIR /build
# Copy your application source
COPY src/ ./src/
COPY CMakeLists.txt .

# Build — static link so the runtime stage needs no .so files
RUN cmake -S . -B _build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    cmake --build _build --target myapp -j$(nproc)


# ── Stage 2: runtime ─────────────────────────────────────────────────────────
FROM rockylinux:9

# Runtime-only libraries (no -devel packages)
RUN dnf install -y dnf-plugins-core && \
    dnf config-manager --set-enabled crb && \
    dnf update -y --allowerasing && \
    dnf install -y --allowerasing \
        libcurl openssl-libs gmp libunistring && \
    dnf clean all && rm -rf /var/cache/dnf

# If you used dynamic linking, also copy the SDK .so files from the builder
COPY --from=builder /usr/local/lib/libubiqclient.so.2.2.4 /usr/local/lib/
RUN cd /usr/local/lib && \
    ln -sf libubiqclient.so.2.2.4 libubiqclient.so.2 && \
    ln -sf libubiqclient.so.2     libubiqclient.so   && \
    ldconfig

# Copy the compiled application
COPY --from=builder /build/_build/myapp /app/myapp

# Credentials are mounted at runtime — never baked into the image
ENTRYPOINT ["/app/myapp"]
```

> **Tip:** If you statically linked (`-lubiqclient` against `libubiqclient.a`),
> omit the `COPY --from=builder /usr/local/lib/libubiqclient*` lines and the
> `ldconfig` call — the binary is self-contained.

---

## 7. Running the Container

```bash
docker run --rm \
  -v "$HOME/.ubiq/credentials:/root/.ubiq/credentials:ro" \
  my-app-image
```

### Verifying the shared library is found (dynamic link only)

```bash
docker run --rm my-app-image ldd /app/myapp | grep ubiq
# expected output:
#   libubiqclient.so.2 => /usr/local/lib/libubiqclient.so.2 (0x...)
```

---

## 8. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `error while loading shared libraries: libubiqclient.so.2` | `.so` not in runtime image or `ldconfig` not run | Add `COPY --from=builder` step and `ldconfig` |
| `HTTP 500` from Ubiq API | HTTP/2 frame mismatch (pre-fix SDK) | Use RPM tag `v2.2.4-rhel9-fix1` or later |
| `No such file or directory: ~/.ubiq/credentials` | Credentials not mounted | Add `-v "$HOME/.ubiq/credentials:/root/.ubiq/credentials:ro"` |
| `undefined reference to ubiq_platform_fpe_encrypt` | Missing `-lubiqclient` at link time | Add flag to gcc/cmake link step |
| `libunistring-devel not found` | CRB repo not enabled | `dnf install -y dnf-plugins-core && dnf config-manager --set-enabled crb` |
