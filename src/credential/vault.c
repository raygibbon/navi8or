#include "nav_credential.h"
#include "store_internal.h"
#include "vault_internal.h"
#include "../platform/secure_file.h"
#include <sodium.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VAULT_HEADER_SIZE 88u
#define PAYLOAD_HEADER_SIZE 12u
#define VAULT_FILE_MAX (VAULT_HEADER_SIZE + NAV_VAULT_PAYLOAD_MAX + \
                        crypto_aead_xchacha20poly1305_ietf_ABYTES)

static const unsigned char vault_magic[8] = {'N','A','V','V','L','T','0','1'};
static const unsigned char payload_magic[4] = {'N','V','P','L'};

typedef struct {
    NavCredential metadata;
    char *secret;
} NavVaultRecord;

typedef struct {
    char *path;
    unsigned char salt[crypto_pwhash_SALTBYTES];
    unsigned long long opslimit;
    size_t memlimit;
    int algorithm;
    unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    bool key_loaded;
    bool locked;
    NavVaultRecord *records;
    size_t count;
} NavVaultBackend;

static void error_set(char *error, size_t size, const char *message)
{ if (error && size) snprintf(error, size, "%s", message); }

static int nav_crypto_init(char *error, size_t error_size)
{
    if (sodium_init() >= 0) return 0;
    error_set(error, error_size, "Unable to initialize libsodium");
    return -1;
}

/* Vault format v1 serializes every integer in big-endian byte order. */
static void write_u32_be(unsigned char *data, uint32_t value)
{
    data[0] = (unsigned char)(value >> 24);
    data[1] = (unsigned char)(value >> 16);
    data[2] = (unsigned char)(value >> 8);
    data[3] = (unsigned char)value;
}

static uint32_t read_u32_be(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void write_u64_be(unsigned char *data, uint64_t value)
{
    for (int index = 7; index >= 0; index--) {
        data[index] = (unsigned char)(value & 255u);
        value >>= 8;
    }
}

static uint64_t read_u64_be(const unsigned char *data)
{
    uint64_t value = 0;
    for (int index = 0; index < 8; index++) value = (value << 8) | data[index];
    return value;
}

static void records_free(NavVaultBackend *vault)
{
    for (size_t index = 0; index < vault->count; index++) {
        if (vault->records[index].secret) {
            sodium_memzero(vault->records[index].secret,
                           strlen(vault->records[index].secret));
            free(vault->records[index].secret);
        }
        sodium_memzero(&vault->records[index], sizeof vault->records[index]);
    }
    free(vault->records);
    vault->records = NULL;
    vault->count = 0;
}

static int valid_name(const char *name)
{
    size_t length = name ? strlen(name) : 0;
    if (!length || length >= NAV_CREDENTIAL_NAME_MAX) return 0;
    for (size_t index = 0; index < length; index++)
        if (iscntrl((unsigned char)name[index])) return 0;
    return 1;
}

static int payload_parse(const unsigned char *data, size_t length,
                         NavVaultRecord **output, size_t *output_count,
                         char *error, size_t error_size)
{
    size_t offset = PAYLOAD_HEADER_SIZE, count;
    NavVaultRecord *records = NULL;
    if (!data || length < PAYLOAD_HEADER_SIZE || length > NAV_VAULT_PAYLOAD_MAX ||
        memcmp(data, payload_magic, 4) || read_u32_be(data + 4) != 1) goto corrupt;
    count = read_u32_be(data + 8);
    if (count > NAV_CREDENTIAL_MAX) goto corrupt;
    if (count && !(records = calloc(count, sizeof *records))) {
        error_set(error, error_size, "Out of memory");
        return -1;
    }
    for (size_t index = 0; index < count; index++) {
        uint8_t type;
        uint16_t name_length, user_length;
        uint32_t secret_length;
        if (offset > length || length - offset < 9) goto corrupt_records;
        type = data[offset++];
        name_length = (uint16_t)(((uint16_t)data[offset] << 8) | data[offset + 1]);
        offset += 2;
        user_length = (uint16_t)(((uint16_t)data[offset] << 8) | data[offset + 1]);
        offset += 2;
        secret_length = read_u32_be(data + offset);
        offset += 4;
        if ((type != NAV_CREDENTIAL_BASIC && type != NAV_CREDENTIAL_BEARER) ||
            !name_length || name_length >= NAV_CREDENTIAL_NAME_MAX ||
            user_length >= NAV_CREDENTIAL_USERNAME_MAX || !secret_length ||
            secret_length > NAV_CREDENTIAL_SECRET_MAX ||
            (size_t)name_length > length - offset ||
            (size_t)user_length > length - offset - name_length ||
            (size_t)secret_length > length - offset - name_length - user_length)
            goto corrupt_records;
        memcpy(records[index].metadata.name, data + offset, name_length);
        offset += name_length;
        memcpy(records[index].metadata.username, data + offset, user_length);
        offset += user_length;
        records[index].metadata.type = (NavCredentialType)type;
        if (!valid_name(records[index].metadata.name) ||
            (type == NAV_CREDENTIAL_BEARER && user_length)) goto corrupt_records;
        for (size_t previous = 0; previous < index; previous++)
            if (!strcmp(records[previous].metadata.name, records[index].metadata.name))
                goto corrupt_records;
        records[index].secret = malloc((size_t)secret_length + 1);
        if (!records[index].secret) {
            error_set(error, error_size, "Out of memory");
            goto fail_records;
        }
        memcpy(records[index].secret, data + offset, secret_length);
        records[index].secret[secret_length] = '\0';
        offset += secret_length;
        if (memchr(records[index].secret, '\0', secret_length)) goto corrupt_records;
    }
    if (offset != length) goto corrupt_records;
    if (output) {
        *output = records;
        *output_count = count;
    } else {
        NavVaultBackend temporary = {.records = records, .count = count};
        records_free(&temporary);
    }
    return 0;
corrupt_records:
    error_set(error, error_size, "Corrupt vault");
fail_records:
    if (records) {
        NavVaultBackend temporary = {.records = records, .count = count};
        records_free(&temporary);
    }
    return -1;
corrupt:
    error_set(error, error_size, "Corrupt vault");
    return -1;
}

int nav_vault_validate_payload(const unsigned char *data, size_t length,
                               char *error, size_t error_size)
{ return payload_parse(data, length, NULL, NULL, error, error_size); }

static int payload_build(const NavVaultBackend *vault, unsigned char **output,
                         size_t *length, char *error, size_t error_size)
{
    size_t total = PAYLOAD_HEADER_SIZE, offset;
    unsigned char *data;
    for (size_t index = 0; index < vault->count; index++) {
        size_t name = strlen(vault->records[index].metadata.name);
        size_t user = strlen(vault->records[index].metadata.username);
        size_t secret = strlen(vault->records[index].secret);
        if (total > NAV_VAULT_PAYLOAD_MAX - 9) {
            error_set(error, error_size, "Vault payload is too large");
            return -1;
        }
        total += 9;
        if (name > NAV_VAULT_PAYLOAD_MAX - total) goto too_large;
        total += name;
        if (user > NAV_VAULT_PAYLOAD_MAX - total) goto too_large;
        total += user;
        if (secret > NAV_VAULT_PAYLOAD_MAX - total) goto too_large;
        total += secret;
    }
    data = sodium_malloc(total);
    if (!data) { error_set(error, error_size, "Out of memory"); return -1; }
    memcpy(data, payload_magic, 4);
    write_u32_be(data + 4, 1);
    write_u32_be(data + 8, (uint32_t)vault->count);
    offset = PAYLOAD_HEADER_SIZE;
    for (size_t index = 0; index < vault->count; index++) {
        const NavVaultRecord *record = &vault->records[index];
        size_t name = strlen(record->metadata.name);
        size_t user = strlen(record->metadata.username);
        size_t secret = strlen(record->secret);
        data[offset++] = (unsigned char)record->metadata.type;
        data[offset++] = (unsigned char)(name >> 8);
        data[offset++] = (unsigned char)name;
        data[offset++] = (unsigned char)(user >> 8);
        data[offset++] = (unsigned char)user;
        write_u32_be(data + offset, (uint32_t)secret);
        offset += 4;
        memcpy(data + offset, record->metadata.name, name); offset += name;
        memcpy(data + offset, record->metadata.username, user); offset += user;
        memcpy(data + offset, record->secret, secret); offset += secret;
    }
    *output = data;
    *length = total;
    return 0;
too_large:
    error_set(error, error_size, "Vault payload is too large");
    return -1;
}

static int header_parse(const unsigned char *file, size_t file_length,
                        NavVaultBackend *vault, uint64_t *cipher_length,
                        uint64_t *plain_length, unsigned char nonce[24],
                        char *error, size_t error_size)
{
    uint64_t stored_memlimit;
    if (!file || file_length < VAULT_HEADER_SIZE || memcmp(file, vault_magic, 8)) {
        error_set(error, error_size, "Corrupt vault");
        return -1;
    }
    if (read_u32_be(file + 8) != 1) {
        error_set(error, error_size, "Unsupported vault format");
        return -1;
    }
    vault->algorithm = (int)read_u32_be(file + 12);
    vault->opslimit = read_u64_be(file + 16);
    stored_memlimit = read_u64_be(file + 24);
    memcpy(vault->salt, file + 32, sizeof vault->salt);
    memcpy(nonce, file + 48, 24);
    *cipher_length = read_u64_be(file + 72);
    *plain_length = read_u64_be(file + 80);
    if (vault->algorithm != crypto_pwhash_ALG_ARGON2ID13 ||
        vault->opslimit < crypto_pwhash_OPSLIMIT_INTERACTIVE ||
        vault->opslimit > crypto_pwhash_OPSLIMIT_SENSITIVE ||
        stored_memlimit < crypto_pwhash_MEMLIMIT_INTERACTIVE ||
        stored_memlimit > crypto_pwhash_MEMLIMIT_SENSITIVE ||
        stored_memlimit > SIZE_MAX || *plain_length > NAV_VAULT_PAYLOAD_MAX ||
        *cipher_length != *plain_length + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        error_set(error, error_size, "Unsupported vault format");
        return -1;
    }
    if (*cipher_length > SIZE_MAX ||
        (size_t)*cipher_length != file_length - VAULT_HEADER_SIZE) {
        error_set(error, error_size, "Corrupt vault");
        return -1;
    }
    vault->memlimit = (size_t)stored_memlimit;
    return 0;
}

static int vault_save(void *backend, char *error, size_t error_size)
{
    NavVaultBackend *vault = backend;
    unsigned char *plain = NULL, *file = NULL;
    size_t plain_length = 0, file_length = 0;
    unsigned long long cipher_length = 0;
    int result = -1;
    if (!vault || vault->locked || !vault->key_loaded) {
        error_set(error, error_size, "Vault locked");
        return -1;
    }
    if (payload_build(vault, &plain, &plain_length, error, error_size)) return -1;
    file_length = VAULT_HEADER_SIZE + plain_length +
                  crypto_aead_xchacha20poly1305_ietf_ABYTES;
    file = malloc(file_length);
    if (!file) { error_set(error, error_size, "Out of memory"); goto done; }
    memcpy(file, vault_magic, 8);
    write_u32_be(file + 8, 1);
    write_u32_be(file + 12, (uint32_t)vault->algorithm);
    write_u64_be(file + 16, vault->opslimit);
    write_u64_be(file + 24, vault->memlimit);
    memcpy(file + 32, vault->salt, crypto_pwhash_SALTBYTES);
    randombytes_buf(file + 48, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    write_u64_be(file + 72,
                 plain_length + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    write_u64_be(file + 80, plain_length);
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            file + VAULT_HEADER_SIZE, &cipher_length, plain, plain_length,
            file, VAULT_HEADER_SIZE, NULL, file + 48, vault->key) ||
        cipher_length != file_length - VAULT_HEADER_SIZE) {
        error_set(error, error_size, "Unable to encrypt vault");
        goto done;
    }
    result = nav_platform_secure_write_atomic(vault->path, file, file_length,
                                              error, error_size);
done:
    if (plain) { sodium_memzero(plain, plain_length); sodium_free(plain); }
    if (file) {
        sodium_memzero(file + VAULT_HEADER_SIZE, file_length - VAULT_HEADER_SIZE);
        free(file);
    }
    return result;
}

static void vault_lock(void *backend);
static void vault_close(void *backend);

int nav_vault_backend_open(void **output, const char *path,
                           char *error, size_t error_size)
{
    NavVaultBackend *vault = NULL;
    unsigned char *file = NULL, nonce[24];
    size_t file_length = 0;
    uint64_t cipher_length, plain_length;
    int result = -1;
    if (output) *output = NULL;
    if (!output || !path) { error_set(error, error_size, "Vault unavailable"); return -1; }
    if (nav_crypto_init(error, error_size)) return -1;
    if (nav_platform_read_file(path, VAULT_FILE_MAX, &file, &file_length,
                               error, error_size)) return -1;
    vault = calloc(1, sizeof *vault);
    if (!vault || !(vault->path = strdup(path))) {
        free(vault); vault = NULL;
        error_set(error, error_size, "Out of memory");
        goto done;
    }
    vault->locked = true;
    if (header_parse(file, file_length, vault, &cipher_length, &plain_length,
                     nonce, error, error_size)) goto done;
    *output = vault;
    vault = NULL;
    result = 0;
done:
    if (file) { sodium_memzero(file, file_length); free(file); }
    if (vault) vault_close(vault);
    return result;
}

int nav_vault_backend_create(void **output, const char *path, const char *password,
                             char *error, size_t error_size)
{
    NavVaultBackend *vault;
    int exists;
    if (output) *output = NULL;
    if (!output || !path) { error_set(error, error_size, "Unable to create vault"); return -1; }
    if (!password || !password[0]) {
        error_set(error, error_size, "Master password must not be empty");
        return -1;
    }
    if (nav_crypto_init(error, error_size)) return -1;
    exists = nav_platform_file_exists(path, error, error_size);
    if (exists < 0) return -1;
    if (exists) { error_set(error, error_size, "Vault already exists"); return -1; }
    vault = calloc(1, sizeof *vault);
    if (!vault || !(vault->path = strdup(path))) {
        free(vault);
        error_set(error, error_size, "Out of memory");
        return -1;
    }
    vault->algorithm = crypto_pwhash_ALG_ARGON2ID13;
    vault->opslimit = crypto_pwhash_OPSLIMIT_INTERACTIVE;
    vault->memlimit = crypto_pwhash_MEMLIMIT_INTERACTIVE;
    randombytes_buf(vault->salt, sizeof vault->salt);
    if (crypto_pwhash(vault->key, sizeof vault->key, password, strlen(password),
                      vault->salt, vault->opslimit, vault->memlimit,
                      vault->algorithm)) {
        vault_close(vault);
        error_set(error, error_size, "Unable to create vault");
        return -1;
    }
    vault->key_loaded = true;
    vault->locked = false;
    if (vault_save(vault, error, error_size)) { vault_close(vault); return -1; }
    *output = vault;
    return 0;
}

static int vault_unlock(void *backend, const char *password,
                        char *error, size_t error_size)
{
    NavVaultBackend *vault = backend;
    unsigned char *file = NULL, *plain = NULL;
    unsigned char nonce[24], key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    size_t file_length = 0;
    uint64_t cipher_length = 0, expected_plain = 0;
    unsigned long long actual_plain = 0;
    NavVaultRecord *records = NULL;
    size_t count = 0;
    int result = -1;
    memset(key, 0, sizeof key);
    if (!vault || !vault->locked) return 0;
    if (!password || !password[0]) goto unable;
    if (nav_platform_read_file(vault->path, VAULT_FILE_MAX, &file, &file_length,
                               error, error_size)) goto done;
    if (header_parse(file, file_length, vault, &cipher_length, &expected_plain,
                     nonce, error, error_size)) goto done;
    if (expected_plain && !(plain = sodium_malloc((size_t)expected_plain))) {
        error_set(error, error_size, "Out of memory");
        goto done;
    }
    if (crypto_pwhash(key, sizeof key, password, strlen(password), vault->salt,
                      vault->opslimit, vault->memlimit, vault->algorithm)) goto unable;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            plain, &actual_plain, NULL, file + VAULT_HEADER_SIZE, cipher_length,
            file, VAULT_HEADER_SIZE, nonce, key) || actual_plain != expected_plain)
        goto unable;
    if (payload_parse(plain, (size_t)actual_plain, &records, &count,
                      error, error_size)) goto done;
    memcpy(vault->key, key, sizeof key);
    vault->key_loaded = true;
    vault->locked = false;
    vault->records = records;
    vault->count = count;
    records = NULL;
    result = 0;
    goto done;
unable:
    error_set(error, error_size, "Unable to unlock vault");
done:
    if (file) { sodium_memzero(file, file_length); free(file); }
    if (plain) { sodium_memzero(plain, (size_t)expected_plain); sodium_free(plain); }
    sodium_memzero(key, sizeof key);
    if (records) {
        NavVaultBackend temporary = {.records = records, .count = count};
        records_free(&temporary);
    }
    return result;
}

static void vault_lock(void *backend)
{
    NavVaultBackend *vault = backend;
    if (!vault) return;
    records_free(vault);
    sodium_memzero(vault->key, sizeof vault->key);
    vault->key_loaded = false;
    vault->locked = true;
}

static void vault_close(void *backend)
{
    NavVaultBackend *vault = backend;
    if (!vault) return;
    vault_lock(vault);
    free(vault->path);
    sodium_memzero(vault, sizeof *vault);
    free(vault);
}

static bool vault_is_locked(const void *backend)
{ const NavVaultBackend *vault = backend; return !vault || vault->locked; }

static size_t vault_count(const void *backend)
{
    const NavVaultBackend *vault = backend;
    return vault && !vault->locked ? vault->count : 0;
}

static const NavCredential *vault_get(const void *backend, size_t index)
{
    const NavVaultBackend *vault = backend;
    return vault && !vault->locked && index < vault->count
        ? &vault->records[index].metadata : NULL;
}

static ptrdiff_t find_record(const NavVaultBackend *vault, const char *name)
{
    if (!name) return -1;
    for (size_t index = 0; index < vault->count; index++)
        if (!strcmp(vault->records[index].metadata.name, name)) return (ptrdiff_t)index;
    return -1;
}

static int vault_put(void *backend, const NavCredential *metadata,
                     const char *secret, bool replace,
                     char *error, size_t error_size)
{
    NavVaultBackend *vault = backend;
    ptrdiff_t found;
    NavVaultRecord *record;
    char *new_secret = NULL, *old_secret = NULL;
    NavCredential old_metadata;
    if (!vault || vault->locked) { error_set(error, error_size, "Vault locked"); return -1; }
    if (!metadata || !valid_name(metadata->name) ||
        (metadata->type != NAV_CREDENTIAL_BASIC &&
         metadata->type != NAV_CREDENTIAL_BEARER) ||
        strnlen(metadata->username, NAV_CREDENTIAL_USERNAME_MAX) >=
            NAV_CREDENTIAL_USERNAME_MAX ||
        (metadata->type == NAV_CREDENTIAL_BEARER && metadata->username[0])) {
        error_set(error, error_size, "Invalid credential metadata");
        return -1;
    }
    found = find_record(vault, metadata->name);
    if (!replace && found >= 0) {
        error_set(error, error_size, "Credential name already exists"); return -1;
    }
    if (replace && found < 0) {
        error_set(error, error_size, "Credential not found"); return -1;
    }
    if (secret && secret[0]) {
        if (strlen(secret) > NAV_CREDENTIAL_SECRET_MAX) {
            error_set(error, error_size, "Secret is too long"); return -1;
        }
        new_secret = strdup(secret);
        if (!new_secret) { error_set(error, error_size, "Out of memory"); return -1; }
    } else if (found < 0) {
        error_set(error, error_size, "Secret must not be empty"); return -1;
    }
    if (found < 0) {
        NavVaultRecord *grown;
        if (vault->count == NAV_CREDENTIAL_MAX) {
            sodium_memzero(new_secret, strlen(new_secret)); free(new_secret);
            error_set(error, error_size, "Credential limit reached"); return -1;
        }
        grown = realloc(vault->records, (vault->count + 1) * sizeof *grown);
        if (!grown) {
            sodium_memzero(new_secret, strlen(new_secret)); free(new_secret);
            error_set(error, error_size, "Out of memory"); return -1;
        }
        vault->records = grown;
        record = &vault->records[vault->count++];
        memset(record, 0, sizeof *record);
    } else {
        record = &vault->records[found];
        old_metadata = record->metadata;
        old_secret = record->secret;
    }
    record->metadata = *metadata;
    if (new_secret) record->secret = new_secret;
    if (vault_save(vault, error, error_size)) {
        if (found < 0) {
            sodium_memzero(record->secret, strlen(record->secret));
            free(record->secret);
            sodium_memzero(record, sizeof *record);
            vault->count--;
        } else {
            record->metadata = old_metadata;
            if (new_secret) {
                sodium_memzero(new_secret, strlen(new_secret));
                free(new_secret);
                record->secret = old_secret;
            }
        }
        return -1;
    }
    if (new_secret && old_secret) {
        sodium_memzero(old_secret, strlen(old_secret));
        free(old_secret);
    }
    return 0;
}

static int vault_delete(void *backend, const char *name,
                        char *error, size_t error_size)
{
    NavVaultBackend *vault = backend;
    ptrdiff_t found;
    NavVaultRecord removed;
    if (!vault || vault->locked) { error_set(error, error_size, "Vault locked"); return -1; }
    found = find_record(vault, name);
    if (found < 0) { error_set(error, error_size, "Credential not found"); return -1; }
    removed = vault->records[found];
    if ((size_t)found + 1 < vault->count)
        memmove(&vault->records[found], &vault->records[found + 1],
                (vault->count - (size_t)found - 1) * sizeof *vault->records);
    vault->count--;
    if (vault_save(vault, error, error_size)) {
        memmove(&vault->records[found + 1], &vault->records[found],
                (vault->count - (size_t)found) * sizeof *vault->records);
        vault->records[found] = removed;
        vault->count++;
        return -1;
    }
    if (removed.secret) {
        sodium_memzero(removed.secret, strlen(removed.secret));
        free(removed.secret);
    }
    sodium_memzero(&removed, sizeof removed);
    return 0;
}

static int vault_resolve(const void *backend, const char *name,
                         NavResolvedCredential *resolved,
                         char *error, size_t error_size)
{
    const NavVaultBackend *vault = backend;
    ptrdiff_t found;
    if (resolved) memset(resolved, 0, sizeof *resolved);
    if (!vault || vault->locked) { error_set(error, error_size, "Vault locked"); return -1; }
    found = find_record(vault, name);
    if (found < 0 || !resolved) {
        error_set(error, error_size, "Credential not found"); return -1;
    }
    resolved->type = vault->records[found].metadata.type;
    resolved->username = strdup(vault->records[found].metadata.username);
    resolved->secret = strdup(vault->records[found].secret);
    if (!resolved->username || !resolved->secret) {
        nav_resolved_credential_free(resolved);
        error_set(error, error_size, "Out of memory");
        return -1;
    }
    return 0;
}

const NavCredentialStoreOps nav_vault_backend_ops = {
    .close = vault_close,
    .unlock = vault_unlock,
    .lock = vault_lock,
    .is_locked = vault_is_locked,
    .count = vault_count,
    .get = vault_get,
    .put = vault_put,
    .delete_credential = vault_delete,
    .resolve = vault_resolve,
    .save = vault_save
};
