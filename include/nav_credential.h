#ifndef NAV_CREDENTIAL_H
#define NAV_CREDENTIAL_H

#include <stdbool.h>
#include <stddef.h>

#define NAV_CREDENTIAL_MAX 256u
#define NAV_CREDENTIAL_NAME_MAX 96u
#define NAV_CREDENTIAL_USERNAME_MAX 256u
#define NAV_CREDENTIAL_SECRET_MAX 65536u
typedef enum {
    NAV_CREDENTIAL_BASIC = 1,
    NAV_CREDENTIAL_BEARER = 2
} NavCredentialType;

typedef struct {
    char name[NAV_CREDENTIAL_NAME_MAX];
    NavCredentialType type;
    char username[NAV_CREDENTIAL_USERNAME_MAX];
} NavCredential;

typedef struct {
    NavCredentialType type;
    /* Resolve returns owned copies; nav_resolved_credential_free wipes them. */
    char *username;
    char *secret;
} NavResolvedCredential;

typedef struct NavCredentialStore NavCredentialStore;

/* Open validates the public header; records remain encrypted until unlock. */
int nav_credential_store_open_vault(NavCredentialStore **, const char *,
                                    char *, size_t);
int nav_credential_store_create_vault(NavCredentialStore **, const char *,
                                      const char *, char *, size_t);
void nav_credential_store_close(NavCredentialStore *);
int nav_credential_store_unlock(NavCredentialStore *, const char *, char *, size_t);
void nav_credential_store_lock(NavCredentialStore *);
bool nav_credential_store_is_locked(const NavCredentialStore *);
size_t nav_credential_store_count(const NavCredentialStore *);
const NavCredential *nav_credential_store_get(const NavCredentialStore *, size_t);
int nav_credential_store_put(NavCredentialStore *, const NavCredential *,
                             const char *, bool, char *, size_t);
int nav_credential_store_delete(NavCredentialStore *, const char *, char *, size_t);
int nav_credential_store_resolve(const NavCredentialStore *, const char *,
                                 NavResolvedCredential *, char *, size_t);
/* Wipes and releases every allocation returned by resolve. */
void nav_resolved_credential_free(NavResolvedCredential *);
/* Wipes caller-owned secret buffers without exposing the crypto dependency. */
void nav_credential_secret_wipe(void *, size_t);
int nav_credential_store_save(NavCredentialStore *, char *, size_t);

#endif
