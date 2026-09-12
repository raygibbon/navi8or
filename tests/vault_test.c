#include "nav_credential.h"
#include "credential/vault_internal.h"
#include "platform/secure_file.h"
#include <sodium.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int contains(const unsigned char *data, size_t length, const char *text)
{
    size_t n = strlen(text);
    for (size_t i = 0; i + n <= length; i++)
        if (!memcmp(data + i, text, n)) return 1;
    return 0;
}

static unsigned char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb"); long size; unsigned char *data;
    assert(file); assert(fseek(file, 0, SEEK_END) == 0); size = ftell(file);
    assert(size > 0); rewind(file); data = malloc((size_t)size); assert(data);
    assert(fread(data, 1, (size_t)size, file) == (size_t)size); fclose(file);
    *length = (size_t)size; return data;
}

static int temporary_file_count(const char *directory)
{
    DIR *entries = opendir(directory);
    struct dirent *entry;
    int count = 0;
    assert(entries);
    while ((entry = readdir(entries)))
        if (strstr(entry->d_name, ".tmp.")) count++;
    assert(closedir(entries) == 0);
    return count;
}

static void fixture_u32(unsigned char *data, uint32_t value)
{
    data[0] = (unsigned char)(value >> 24);
    data[1] = (unsigned char)(value >> 16);
    data[2] = (unsigned char)(value >> 8);
    data[3] = (unsigned char)value;
}

static void fixture_u64(unsigned char *data, uint64_t value)
{
    for (int index = 7; index >= 0; index--) {
        data[index] = (unsigned char)value;
        value >>= 8;
    }
}

/* Independently construct a deterministic format-v1 file. This represents the
   pre-refactor wire format and catches byte-order/header-layout drift. */
static void write_compatibility_fixture(const char *path)
{
    static const unsigned char name[] = "legacy";
    static const unsigned char secret[] = "fixture-token";
    static const char password[] = "fixture-password";
    enum { header_size = 88, payload_size = 12 + 9 + 6 + 13,
           file_size = header_size + payload_size + 16 };
    unsigned char file[file_size], payload[payload_size], key[32];
    unsigned long long cipher_length = 0;
    FILE *stream;
    assert(sodium_init() >= 0);
    memset(file, 0, sizeof file);
    memcpy(file, "NAVVLT01", 8);
    fixture_u32(file + 8, 1);
    fixture_u32(file + 12, crypto_pwhash_ALG_ARGON2ID13);
    fixture_u64(file + 16, crypto_pwhash_OPSLIMIT_INTERACTIVE);
    fixture_u64(file + 24, crypto_pwhash_MEMLIMIT_INTERACTIVE);
    for (size_t index = 0; index < crypto_pwhash_SALTBYTES; index++)
        file[32 + index] = (unsigned char)index;
    for (size_t index = 0; index < crypto_aead_xchacha20poly1305_ietf_NPUBBYTES; index++)
        file[48 + index] = (unsigned char)(32 + index);
    fixture_u64(file + 72, payload_size + 16);
    fixture_u64(file + 80, payload_size);
    memcpy(payload, "NVPL", 4);
    fixture_u32(payload + 4, 1);
    fixture_u32(payload + 8, 1);
    payload[12] = NAV_CREDENTIAL_BEARER;
    payload[13] = 0; payload[14] = sizeof name - 1;
    payload[15] = 0; payload[16] = 0;
    fixture_u32(payload + 17, sizeof secret - 1);
    memcpy(payload + 21, name, sizeof name - 1);
    memcpy(payload + 21 + sizeof name - 1, secret, sizeof secret - 1);
    assert(crypto_pwhash(key, sizeof key, password, strlen(password), file + 32,
                         crypto_pwhash_OPSLIMIT_INTERACTIVE,
                         crypto_pwhash_MEMLIMIT_INTERACTIVE,
                         crypto_pwhash_ALG_ARGON2ID13) == 0);
    assert(crypto_aead_xchacha20poly1305_ietf_encrypt(
               file + header_size, &cipher_length, payload, sizeof payload,
               file, header_size, NULL, file + 48, key) == 0);
    assert(cipher_length == payload_size + 16);
    stream = fopen(path, "wb"); assert(stream);
    assert(fwrite(file, 1, sizeof file, stream) == sizeof file);
    assert(fclose(stream) == 0);
    sodium_memzero(key, sizeof key);
    sodium_memzero(payload, sizeof payload);
    sodium_memzero(file, sizeof file);
}

int main(void)
{
    char directory[] = "/tmp/navi8or-vault-test-XXXXXX", path[512], error[256];
    NavCredentialStore *store = NULL; NavResolvedCredential resolved;
    NavCredential basic = {.name = "local-basic", .type = NAV_CREDENTIAL_BASIC,
                           .username = "navi8or"};
    NavCredential bearer = {.name = "local-bearer", .type = NAV_CREDENTIAL_BEARER};
    unsigned char *original, *data; size_t length; struct stat status;
    assert(mkdtemp(directory));
    snprintf(path, sizeof path, "%s/vault.bin", directory);

    assert(nav_credential_store_create_vault(&store, path, "", error, sizeof error) != 0);
    assert(!strcmp(error, "Master password must not be empty") && !store);

    assert(nav_credential_store_create_vault(&store, path, "correct-password", error, sizeof error) == 0);
    assert(!nav_credential_store_is_locked(store));
    assert(nav_credential_store_count(store) == 0);
    assert(stat(path, &status) == 0 && (status.st_mode & 0777) == 0600);
    assert(nav_credential_store_put(store, &basic, "testpass", false, error, sizeof error) == 0);
    assert(nav_credential_store_put(store, &bearer, "navi8or-test-token-12345", false, error, sizeof error) == 0);
    assert(nav_credential_store_put(store, &basic, "duplicate", false, error, sizeof error) != 0);
    assert(nav_credential_store_count(store) == 2);
    data = read_file(path, &length);
    assert(!contains(data, length, "testpass"));
    assert(!contains(data, length, "navi8or-test-token-12345"));
    free(data);

    nav_credential_store_lock(store);
    assert(nav_credential_store_is_locked(store));
    assert(nav_credential_store_count(store) == 0);
    assert(nav_credential_store_get(store, 0) == NULL);
    assert(nav_credential_store_resolve(store, "local-basic", &resolved,
                                        error, sizeof error) != 0);
    assert(!strcmp(error, "Vault locked"));
    assert(nav_credential_store_unlock(store, "wrong-password", error, sizeof error) != 0);
    assert(!strcmp(error, "Unable to unlock vault"));
    assert(nav_credential_store_is_locked(store));
    assert(nav_credential_store_unlock(store, "correct-password", error, sizeof error) == 0);
    assert(nav_credential_store_count(store) == 2);
    assert(nav_credential_store_resolve(store, "local-basic", &resolved, error, sizeof error) == 0);
    assert(resolved.type == NAV_CREDENTIAL_BASIC && !strcmp(resolved.username, "navi8or") &&
           !strcmp(resolved.secret, "testpass"));
    nav_resolved_credential_free(&resolved);
    assert(nav_credential_store_resolve(store, "local-bearer", &resolved, error, sizeof error) == 0);
    assert(resolved.type == NAV_CREDENTIAL_BEARER && !resolved.username[0] &&
           !strcmp(resolved.secret, "navi8or-test-token-12345"));
    nav_resolved_credential_free(&resolved);

    snprintf(basic.username, sizeof basic.username, "updated-user");
    assert(nav_credential_store_put(store, &basic, NULL, true, error, sizeof error) == 0);
    assert(nav_credential_store_resolve(store, basic.name, &resolved, error, sizeof error) == 0);
    assert(!strcmp(resolved.username, "updated-user") && !strcmp(resolved.secret, "testpass"));
    nav_resolved_credential_free(&resolved);
    assert(nav_credential_store_put(store, &basic, "new-password", true, error, sizeof error) == 0);
    assert(nav_credential_store_put(store, &bearer, "new-token", true, error, sizeof error) == 0);
    nav_credential_store_lock(store);
    assert(nav_credential_store_unlock(store, "correct-password", error, sizeof error) == 0);
    assert(nav_credential_store_resolve(store, basic.name, &resolved, error, sizeof error) == 0);
    assert(!strcmp(resolved.secret, "new-password")); nav_resolved_credential_free(&resolved);
    assert(nav_credential_store_resolve(store, bearer.name, &resolved, error, sizeof error) == 0);
    assert(!strcmp(resolved.secret, "new-token")); nav_resolved_credential_free(&resolved);

    original = read_file(path, &length);
    data = malloc(length); assert(data); memcpy(data, original, length); data[length - 1] ^= 1;
    { FILE *file = fopen(path, "wb"); assert(file); assert(fwrite(data, 1, length, file) == length); fclose(file); }
    free(data); nav_credential_store_close(store); store = NULL;
    assert(nav_credential_store_open_vault(&store, path, error, sizeof error) == 0);
    assert(nav_credential_store_unlock(store, "correct-password", error, sizeof error) != 0);
    assert(nav_credential_store_is_locked(store) && nav_credential_store_count(store) == 0);
    { FILE *file = fopen(path, "wb"); assert(file); assert(fwrite(original, 1, length, file) == length); fclose(file); }
    nav_credential_store_close(store); store = NULL;

    data = malloc(length); assert(data); memcpy(data, original, length); data[11] = 2;
    { FILE *file = fopen(path, "wb"); assert(file); assert(fwrite(data, 1, length, file) == length); fclose(file); }
    free(data);
    assert(nav_credential_store_open_vault(&store, path, error, sizeof error) != 0);
    assert(!strcmp(error, "Unsupported vault format") && !store);
    { FILE *file = fopen(path, "wb"); assert(file); assert(fwrite(original, 1, length, file) == length); fclose(file); }

    /* Stored lengths and actual file size must agree exactly. */
    { FILE *file = fopen(path, "ab"); assert(file); assert(fputc(0, file) == 0); fclose(file); }
    assert(nav_credential_store_open_vault(&store, path, error, sizeof error) != 0);
    assert(!strcmp(error, "Corrupt vault") && !store);
    { FILE *file = fopen(path, "wb"); assert(file); assert(fwrite(original, 1, length, file) == length); fclose(file); }
    free(original);

    assert(nav_credential_store_open_vault(&store, path, error, sizeof error) == 0);
    assert(nav_credential_store_unlock(store, "correct-password", error, sizeof error) == 0);
    assert(nav_credential_store_delete(store, bearer.name, error, sizeof error) == 0);
    nav_credential_store_close(store); store = NULL;
    assert(nav_credential_store_open_vault(&store, path, error, sizeof error) == 0);
    assert(nav_credential_store_unlock(store, "correct-password", error, sizeof error) == 0);
    assert(nav_credential_store_count(store) == 1);
    assert(nav_credential_store_resolve(store, bearer.name, &resolved, error, sizeof error) != 0);

    /* A failed pre-rename save preserves disk/state and removes its temp file. */
    {
        NavCredential failed = *nav_credential_store_get(store, 0);
        snprintf(failed.username, sizeof failed.username, "must-not-persist");
        nav_platform_secure_file_fail_next_write();
        assert(nav_credential_store_put(store, &failed, NULL, true, error, sizeof error) != 0);
        assert(strstr(error, "Unable to replace vault"));
        assert(temporary_file_count(directory) == 0);
        assert(strcmp(nav_credential_store_get(store, 0)->username, "must-not-persist"));
        nav_credential_store_close(store); store = NULL;
        assert(nav_credential_store_open_vault(&store, path, error, sizeof error) == 0);
        assert(nav_credential_store_unlock(store, "correct-password", error, sizeof error) == 0);
        assert(strcmp(nav_credential_store_get(store, 0)->username, "must-not-persist"));
    }
    nav_credential_store_close(store);
    store = NULL;

    write_compatibility_fixture(path);
    assert(nav_credential_store_open_vault(&store, path, error, sizeof error) == 0);
    assert(nav_credential_store_unlock(store, "fixture-password", error, sizeof error) == 0);
    assert(nav_credential_store_resolve(store, "legacy", &resolved, error, sizeof error) == 0);
    assert(resolved.type == NAV_CREDENTIAL_BEARER && !resolved.username[0]);
    assert(!strcmp(resolved.secret, "fixture-token"));
    nav_resolved_credential_free(&resolved);
    nav_credential_store_close(store);
    store = NULL;

    /* Bounded platform reads reject oversized files before allocating them. */
    {
        FILE *file = fopen(path, "wb");
        assert(file);
        assert(ftruncate(fileno(file), (off_t)NAV_VAULT_PAYLOAD_MAX + 105) == 0);
        assert(fclose(file) == 0);
    }
    assert(nav_credential_store_open_vault(&store, path, error, sizeof error) != 0);
    assert(!strcmp(error, "Vault file is too large") && !store);

    /* Vault parsing stays private while retaining focused corruption coverage. */
    {
        const unsigned char invalid_length[] = {
            'N','V','P','L',0,0,0,1,0,0,0,1,
            NAV_CREDENTIAL_BASIC,0,1,0,0,0,0,0,0,'a'
        };
        const unsigned char duplicate_records[] = {
            'N','V','P','L',0,0,0,1,0,0,0,2,
            NAV_CREDENTIAL_BEARER,0,1,0,0,0,0,0,1,'a','x',
            NAV_CREDENTIAL_BEARER,0,1,0,0,0,0,0,1,'a','y'
        };
        const unsigned char invalid_type[] = {
            'N','V','P','L',0,0,0,1,0,0,0,1,
            99,0,1,0,0,0,0,0,1,'a','x'
        };
        const unsigned char truncated[] = {
            'N','V','P','L',0,0,0,1,0,0,0,1,NAV_CREDENTIAL_BASIC
        };
        const unsigned char excessive_count[] = {
            'N','V','P','L',0,0,0,1,0,0,1,1
        };
        const unsigned char excessive_name[] = {
            'N','V','P','L',0,0,0,1,0,0,0,1,
            NAV_CREDENTIAL_BASIC,0,NAV_CREDENTIAL_NAME_MAX,0,0,0,0,0,1
        };
        const unsigned char excessive_secret[] = {
            'N','V','P','L',0,0,0,1,0,0,0,1,
            NAV_CREDENTIAL_BASIC,0,1,0,0,0,1,0,1
        };
        const unsigned char *cases[] = {
            invalid_length, duplicate_records, invalid_type, truncated,
            excessive_count, excessive_name, excessive_secret
        };
        const size_t sizes[] = {
            sizeof invalid_length, sizeof duplicate_records, sizeof invalid_type,
            sizeof truncated, sizeof excessive_count, sizeof excessive_name,
            sizeof excessive_secret
        };
        for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
            assert(nav_vault_validate_payload(cases[index], sizes[index],
                                              error, sizeof error) != 0);
            assert(!strcmp(error, "Corrupt vault"));
        }
    }
    unlink(path); rmdir(directory);
    puts("vault credential store: ok");
    return 0;
}
