/* fpe_test.c
 * FPE encrypt/decrypt round-trip test for the Ubiq SDK.
 * Loads credentials from ~/.ubiq/credentials (default profile).
 *
 * Compile (after SDK install):
 *   gcc fpe_test.c -I/usr/local/include -L/usr/local/lib \
 *       -lubiqclient -lssl -lcrypto -lcurl -lgmp -lunistring \
 *       -o fpe_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ubiq/platform.h>

#define MAX_BUFFER_LEN 255

static void printResult(const char * text, int result)
{
    printf("%s (%d): ", text, result);
    switch (-result) {
        case 0:      printf("Success\n"); break;
        case EINVAL: printf("Invalid parameter — check length bounds and character set\n"); break;
        case 401:    printf("Unauthorized — check credentials and FFS is linked to app\n"); break;
        default:     printf("%s\n", strerror(-result)); break;
    }
}

int main(void)
{
    struct ubiq_platform_credentials * credentials = NULL;
    int result;

    const char * FFS_NAME = "AName";
    const char * test_pt  = "sathishkumar";

    char * test_ct    = NULL;
    char * ptdest_buf = NULL;
    size_t ptdest_len = 0;
    size_t ct_len     = 0;
    char persistent[MAX_BUFFER_LEN];
    memset(persistent, 0, sizeof(persistent));

    printf("Initializing UBIQ library...\n");
    result = ubiq_platform_init();
    printResult("Init", result);

    result = ubiq_platform_credentials_create(&credentials);
    printResult("Credentials from ~/.ubiq/credentials", result);
    if (result) { ubiq_platform_exit(); exit(result); }

    printf("\nDataset  : %s\n", FFS_NAME);
    printf("Plaintext: %s (len=%zu)\n\n", test_pt, strlen(test_pt));

    /* ── Encrypt ──────────────────────────────────────────────────────────── */
    result = ubiq_platform_fpe_encrypt(
        credentials,
        FFS_NAME,
        NULL, 0,
        test_pt, strlen(test_pt),
        &test_ct, &ct_len);
    printResult("Encrypt", result);

    if (result < 0) {
        printf("Encryption failed. Aborting.\n");
        ubiq_platform_credentials_destroy(credentials);
        ubiq_platform_exit();
        exit(result);
    }

    printf("Ciphertext: %s (len=%zu)\n\n", test_ct, ct_len);
    strncpy(persistent, test_ct, MAX_BUFFER_LEN - 1);
    free(test_ct);

    /* ── Decrypt ──────────────────────────────────────────────────────────── */
    result = ubiq_platform_fpe_decrypt(
        credentials,
        FFS_NAME,
        NULL, 0,
        persistent, strlen(persistent),
        &ptdest_buf, &ptdest_len);
    printResult("Decrypt", result);

    if (result < 0) {
        printf("Decryption failed.\n");
        free(ptdest_buf);
        ubiq_platform_credentials_destroy(credentials);
        ubiq_platform_exit();
        exit(result);
    }

    printf("Decrypted : %.*s (len=%zu)\n\n", (int)ptdest_len, ptdest_buf, ptdest_len);

    /* ── Verify round-trip ────────────────────────────────────────────────── */
    if (ptdest_len == strlen(test_pt) &&
        memcmp(test_pt, ptdest_buf, ptdest_len) == 0) {
        printf("PASS: Round-trip verified — FPE encryption working!\n");
    } else {
        printf("FAIL: Round-trip MISMATCH\n");
    }

    free(ptdest_buf);
    ubiq_platform_credentials_destroy(credentials);
    ubiq_platform_exit();
    return 0;
}
