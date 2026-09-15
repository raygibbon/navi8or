#ifndef NAV_CREDENTIAL_STORE_INTERNAL_H
#define NAV_CREDENTIAL_STORE_INTERNAL_H

#include "nav_credential.h"

typedef struct NavCredentialStoreOps {
    void (*close)(void *backend);
    int (*unlock)(void *backend, const char *password, char *error, size_t error_size);
    void (*lock)(void *backend);
    bool (*is_locked)(const void *backend);
    size_t (*count)(const void *backend);
    const NavCredential *(*get)(const void *backend, size_t index);
    int (*put)(void *backend, const NavCredential *metadata, const char *secret,
               bool replace, char *error, size_t error_size);
    int (*delete_credential)(void *backend, const char *name,
                             char *error, size_t error_size);
    int (*resolve)(const void *backend, const char *name,
                   NavResolvedCredential *resolved, char *error, size_t error_size);
    int (*save)(void *backend, char *error, size_t error_size);
} NavCredentialStoreOps;

struct NavCredentialStore {
    /* The store owns backend and destroys it through ops->close. Backends own
       their decrypted records; only resolved copies cross the public seam. */
    const NavCredentialStoreOps *ops;
    void *backend;
};

int nav_credential_store_wrap(NavCredentialStore **output,
                              const NavCredentialStoreOps *ops, void *backend,
                              char *error, size_t error_size);

int nav_vault_backend_open(void **output, const char *path,
                           char *error, size_t error_size);
int nav_vault_backend_create(void **output, const char *path, const char *password,
                             char *error, size_t error_size);
extern const NavCredentialStoreOps nav_vault_backend_ops;

#endif
