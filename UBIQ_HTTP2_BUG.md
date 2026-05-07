# Bug Report: HTTP/2 POST Frame Mismatch in Ubiq C SDK v2.2.4

## Summary

The Ubiq C/C++ SDK v2.2.4 fails with `HTTP 500 Internal Server Error` on any
platform where `libcurl` is built with `nghttp2` (HTTP/2 support) and the Ubiq
API server negotiates HTTP/2 via ALPN. The root cause is an incorrect use of
`CURLOPT_UPLOAD` combined with `CURLOPT_CUSTOMREQUEST` in `src/lib/curl.c`.

---

## Affected Platforms

| Platform         | curl version  | nghttp2 version | Status   |
|------------------|---------------|-----------------|----------|
| macOS 15.x       | 8.7.1         | bundled         | AFFECTED |
| RHEL 9.7 (Plow)  | 7.76+         | 1.43.0          | AFFECTED |
| Any Linux/macOS  | Any with HTTP/2| Any            | AFFECTED |

To check if your platform is affected:
```bash
curl --version | grep nghttp2
```
If `nghttp2` appears in the output, the platform is affected.

---

## Root Cause

In `src/lib/curl.c`, the `ubiq_support_http_request()` function uses this
pattern to send POST requests with a body:

```c
/* Step 1: sets HTTP/2 stream frame type to PUT at protocol level */
curl_easy_setopt(hnd->ch, CURLOPT_UPLOAD, 1L);

/* Step 2: overrides the method header to POST */
/* but does NOT change the HTTP/2 frame type  */
curl_easy_setopt(hnd->ch, CURLOPT_CUSTOMREQUEST, "POST");
```

### Why this works on HTTP/1.1 but fails on HTTP/2

Under **HTTP/1.1**, the method is purely a text header (`POST /path HTTP/1.1`).
`CURLOPT_CUSTOMREQUEST` overrides it cleanly — the server sees `POST` and
processes it correctly.

Under **HTTP/2**, the method is encoded as a pseudo-header (`:method`) in a
DATA frame. `CURLOPT_UPLOAD=1` sets the frame-level method to `PUT`.
`CURLOPT_CUSTOMREQUEST` changes the `:method` pseudo-header value to `POST`
but **does not change the frame type**. The server receives a `POST`
pseudo-header on a `PUT`-framed stream — a protocol-level contradiction — and
responds with `HTTP 500 Internal Server Error`.

### Request flow with the bug

```
Client (curl with nghttp2)          Ubiq API Server
─────────────────────────           ───────────────
SETTINGS frame          ──────────>
                        <────────── SETTINGS frame
HEADERS frame           ──────────>
  :method = POST   <── CURLOPT_CUSTOMREQUEST
  :path = /api/v0/encryption/key
  [frame type = PUT] <── CURLOPT_UPLOAD=1  ← MISMATCH
DATA frame              ──────────>
  {"uses":1,"dataset":"AName"}
                        <────────── HTTP 500 Internal Server Error
```

---

## Symptoms

- `ubiq_platform_encrypt()` or `ubiq_platform_fpe_encrypt()` returns `-53`
  (`-ECONNABORTED`) which maps to an HTTP 5xx server error
- Enabling curl verbose (`CURLOPT_VERBOSE`) shows `HTTP/1.1 500 Internal
  Server Error` (or HTTP/2 500)
- The `x-runtime` in the response is very short (~20ms), indicating the server
  rejected the request immediately without processing
- GET requests (FFS metadata fetch) succeed with HTTP 200; only POST requests
  (key fetch) fail

---

## Fix Applied

### Workaround (applied in this patch)

Force HTTP/1.1 by adding a single line immediately after `CURLOPT_URL` is set:

```c
curl_easy_setopt(hnd->ch, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
```

**File:** `src/lib/curl.c`
**Function:** `ubiq_support_http_request()`
**Location:** Immediately after `curl_easy_setopt(hnd->ch, CURLOPT_URL, urlstr)`

This forces HTTP/1.1 for all SDK requests, eliminating the frame type mismatch
while preserving full functionality.

### Proper Long-Term Fix (recommended for Ubiq SDK team)

Replace the `CURLOPT_UPLOAD` pattern with `CURLOPT_POST`, which correctly sets
both the HTTP/2 frame type and the method header to POST:

```c
/* BEFORE (buggy): */
curl_easy_setopt(hnd->ch, CURLOPT_UPLOAD, 1L);
curl_easy_setopt(hnd->ch, CURLOPT_READDATA, hnd);
curl_easy_setopt(hnd->ch, CURLOPT_READFUNCTION, &ubiq_support_http_upload);
curl_easy_setopt(hnd->ch, CURLOPT_INFILESIZE, length);
/* ... later ... */
curl_easy_setopt(hnd->ch, CURLOPT_CUSTOMREQUEST, "POST");

/* AFTER (correct): */
curl_easy_setopt(hnd->ch, CURLOPT_POST, 1L);
curl_easy_setopt(hnd->ch, CURLOPT_POSTFIELDS, content);
curl_easy_setopt(hnd->ch, CURLOPT_POSTFIELDSIZE, (long)length);
/* CURLOPT_CUSTOMREQUEST is no longer needed for POST */
```

This approach:
- Correctly sets HTTP/2 frame type to POST
- Works identically on HTTP/1.1
- Preserves HTTP/2 compatibility (no need to force HTTP/1.1)
- Eliminates the need for the custom read callback for POST requests

---

## How to Apply the Workaround Patch

### macOS
```bash
sed -i '' 's|if ((rc = curl_easy_setopt(hnd->ch, CURLOPT_URL, urlstr)) == CURLE_OK) {|if ((rc = curl_easy_setopt(hnd->ch, CURLOPT_URL, urlstr)) == CURLE_OK) {\n        curl_easy_setopt(hnd->ch, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);|' src/lib/curl.c
```

### Linux (RHEL / Ubuntu / Debian)
```bash
sed -i 's|if ((rc = curl_easy_setopt(hnd->ch, CURLOPT_URL, urlstr)) == CURLE_OK) {|if ((rc = curl_easy_setopt(hnd->ch, CURLOPT_URL, urlstr)) == CURLE_OK) {\n        curl_easy_setopt(hnd->ch, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);|' src/lib/curl.c
```

### Or replace the file directly
```bash
cp curl.c src/lib/curl.c
```

---

## Build Instructions — RHEL 9.7 (Plow)

### Step 1 — Install dependencies
```bash
sudo dnf install -y \
  git cmake gcc gcc-c++ make \
  openssl-devel \
  libcurl-devel \
  gmp-devel \
  libunistring-devel \
  boost-devel
```

### Step 2 — Clone and apply fix
```bash
git clone https://gitlab.com/ubiqsecurity/ubiq-c-cpp.git
cd ubiq-c-cpp
git submodule update --init --recursive

# Apply the HTTP/2 fix
sed -i 's|if ((rc = curl_easy_setopt(hnd->ch, CURLOPT_URL, urlstr)) == CURLE_OK) {|if ((rc = curl_easy_setopt(hnd->ch, CURLOPT_URL, urlstr)) == CURLE_OK) {\n        curl_easy_setopt(hnd->ch, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);|' src/lib/curl.c

# Verify patch applied
grep -n "HTTP_VERSION" src/lib/curl.c
```

### Step 3 — Build
```bash
mkdir build && cd build

cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-Wno-implicit-function-declaration"

make -j$(nproc)
sudo make install
```

### Step 4 — Verify installation
```bash
ls /usr/local/lib/libubiq*
ls /usr/local/include/ubiq/
```

### Step 5 — Compile your application
```bash
gcc your_app.c \
  -I/usr/local/include \
  -lubiqclient -lssl -lcrypto -lcurl -lgmp -lunistring \
  -o your_app
```

### Step 6 — Credentials file
```bash
mkdir -p ~/.ubiq
cat > ~/.ubiq/credentials << 'EOF'
[default]
ACCESS_KEY_ID = your_access_key_id
SECRET_SIGNING_KEY = your_secret_signing_key
SECRET_CRYPTO_ACCESS_KEY = your_secret_crypto_access_key
EOF
chmod 600 ~/.ubiq/credentials
```

---

## Verification Test

```c
/* fpe_test.c - verify FPE encrypt/decrypt works after fix */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ubiq/platform.h>

int main(void)
{
    struct ubiq_platform_credentials * creds = NULL;
    const char * FFS_NAME  = "AName";          /* your dataset name */
    char * plaintext       = "sathishkumar";   /* min 8 chars       */
    char * ciphertext      = NULL;
    char * recovered       = NULL;
    size_t ct_len = 0, pt_len = 0;
    int res;

    ubiq_platform_init();
    res = ubiq_platform_credentials_create(&creds);
    if (res) { fprintf(stderr, "Credentials failed: %d\n", res); return 1; }

    res = ubiq_platform_fpe_encrypt(creds, FFS_NAME, NULL, 0,
                                    plaintext, strlen(plaintext),
                                    &ciphertext, &ct_len);
    if (res) { fprintf(stderr, "Encrypt failed: %d\n", res); return 1; }
    printf("Encrypted : %s\n", ciphertext);

    res = ubiq_platform_fpe_decrypt(creds, FFS_NAME, NULL, 0,
                                    ciphertext, ct_len,
                                    &recovered, &pt_len);
    if (res) { fprintf(stderr, "Decrypt failed: %d\n", res); return 1; }
    printf("Decrypted : %.*s\n", (int)pt_len, recovered);

    if (memcmp(plaintext, recovered, pt_len) == 0)
        printf("PASS: Round-trip verified\n");
    else
        printf("FAIL: Mismatch\n");

    free(ciphertext);
    free(recovered);
    ubiq_platform_credentials_destroy(creds);
    ubiq_platform_exit();
    return 0;
}
```

Compile and run:
```bash
gcc fpe_test.c -I/usr/local/include \
  -lubiqclient -lssl -lcrypto -lcurl -lgmp -lunistring \
  -o fpe_test && ./fpe_test
```

Expected output:
```
Encrypted : <ciphertext matching input format>
Decrypted : sathishkumar
PASS: Round-trip verified
```

---

## References

- curl: `CURLOPT_UPLOAD` — https://curl.se/libcurl/c/CURLOPT_UPLOAD.html
- curl: `CURLOPT_POST` — https://curl.se/libcurl/c/CURLOPT_POST.html
- curl: `CURLOPT_HTTP_VERSION` — https://curl.se/libcurl/c/CURLOPT_HTTP_VERSION.html
- HTTP/2 RFC 7540 Section 8.1.2.3 — Request Pseudo-Header Fields

---

## Reported Against

**Repository:** https://gitlab.com/ubiqsecurity/ubiq-c-cpp  
**Version:** v2.2.4.0  
**File:** `src/lib/curl.c`  
**Function:** `ubiq_support_http_request()`
