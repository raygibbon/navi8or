#include "nav_credential.h"
#include "store_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void error_set(char *error, size_t size, const char *message)
{ if (error && size) snprintf(error, size, "%s", message); }

int nav_credential_store_wrap(NavCredentialStore **output,
                              const NavCredentialStoreOps *ops, void *backend,
                              char *error, size_t error_size)
{
    NavCredentialStore *store;
    if (!output || !ops || !backend) {
        error_set(error, error_size, "Unable to initialize credential store");
        return -1;
    }
    store = malloc(sizeof *store);
    if (!store) {
        error_set(error, error_size, "Out of memory");
        return -1;
    }
    store->ops = ops;
    store->backend = backend;
    *output = store;
    return 0;
}

int nav_credential_store_open_vault(NavCredentialStore **output, const char *path,
                                    char *error, size_t error_size)
{
    void *backend = NULL;
    if (output) *output = NULL;
    if (nav_vault_backend_open(&backend, path, error, error_size)) return -1;
    if (nav_credential_store_wrap(output, &nav_vault_backend_ops, backend,
                                  error, error_size)) {
        nav_vault_backend_ops.close(backend);
        return -1;
    }
    return 0;
}

int nav_credential_store_create_vault(NavCredentialStore **output, const char *path,
                                      const char *password,
                                      char *error, size_t error_size)
{
    void *backend = NULL;
    if (output) *output = NULL;
    if (nav_vault_backend_create(&backend, path, password, error, error_size)) return -1;
    if (nav_credential_store_wrap(output, &nav_vault_backend_ops, backend,
                                  error, error_size)) {
        nav_vault_backend_ops.close(backend);
        return -1;
    }
    return 0;
}

void nav_credential_store_close(NavCredentialStore *store)
{
    if (!store) return;
    store->ops->close(store->backend);
    nav_credential_secret_wipe(store, sizeof *store);
    free(store);
}

int nav_credential_store_unlock(NavCredentialStore *store, const char *password,
                                char *error, size_t error_size)
{
    if (!store) { error_set(error, error_size, "Vault unavailable"); return -1; }
    return store->ops->unlock(store->backend, password, error, error_size);
}

void nav_credential_store_lock(NavCredentialStore *store)
{ if (store) store->ops->lock(store->backend); }

bool nav_credential_store_is_locked(const NavCredentialStore *store)
{ return !store || store->ops->is_locked(store->backend); }

size_t nav_credential_store_count(const NavCredentialStore *store)
{ return store ? store->ops->count(store->backend) : 0; }

const NavCredential *nav_credential_store_get(const NavCredentialStore *store, size_t index)
{ return store ? store->ops->get(store->backend, index) : NULL; }

int nav_credential_store_put(NavCredentialStore *store, const NavCredential *metadata,
                             const char *secret, bool replace,
                             char *error, size_t error_size)
{
    if (!store) { error_set(error, error_size, "Vault unavailable"); return -1; }
    return store->ops->put(store->backend, metadata, secret, replace, error, error_size);
}

int nav_credential_store_delete(NavCredentialStore *store, const char *name,
                                char *error, size_t error_size)
{
    if (!store) { error_set(error, error_size, "Vault unavailable"); return -1; }
    return store->ops->delete_credential(store->backend, name, error, error_size);
}

int nav_credential_store_resolve(const NavCredentialStore *store, const char *name,
                                 NavResolvedCredential *resolved,
                                 char *error, size_t error_size)
{
    if (!store) {
        if (resolved) memset(resolved, 0, sizeof *resolved);
        error_set(error, error_size, "Vault unavailable");
        return -1;
    }
    return store->ops->resolve(store->backend, name, resolved, error, error_size);
}

int nav_credential_store_save(NavCredentialStore *store, char *error, size_t error_size)
{
    if (!store) { error_set(error, error_size, "Vault unavailable"); return -1; }
    return store->ops->save(store->backend, error, error_size);
}

void nav_credential_secret_wipe(void *data, size_t size)
{
    volatile unsigned char *byte = data;
    while (byte && size--) *byte++ = 0;
}

void nav_resolved_credential_free(NavResolvedCredential *resolved)
{
    if (!resolved) return;
    if (resolved->username) {
        nav_credential_secret_wipe(resolved->username, strlen(resolved->username));
        free(resolved->username);
    }
    if (resolved->secret) {
        nav_credential_secret_wipe(resolved->secret, strlen(resolved->secret));
        free(resolved->secret);
    }
    nav_credential_secret_wipe(resolved, sizeof *resolved);
}
