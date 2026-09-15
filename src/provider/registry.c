#include "nav.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef struct
{
    const char *name;
    const char *const *schemes;
    bool (*supports_credential)(NavCredentialType);
    NavProvider *(*open_local)(void);
    NavProvider *(*create_repository)(const NavRepository *, NavCredentialStore *,
                                     char *, size_t);
} ProviderType;

static bool http_supports_credential(NavCredentialType type)
{
    return type == NAV_CREDENTIAL_BASIC || type == NAV_CREDENTIAL_BEARER;
}

static bool smb_supports_credential(NavCredentialType type)
{
    return type == NAV_CREDENTIAL_BASIC;
}

static const char *const http_schemes[] = {"http", "https", NULL};
static const char *const smb_schemes[] = {"smb", NULL};
static const ProviderType provider_types[] = {
    {"Local Filesystem", NULL, NULL, nav_local_provider, NULL},
    {"HTTP", http_schemes, http_supports_credential, NULL, nav_http_provider_create},
    {"SMB", smb_schemes, smb_supports_credential, NULL, nav_smb_provider_create},
};

static bool scheme_letter(unsigned char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

/* Only an explicit scheme:// selects a remote transport. In particular,
   report:2026, ./http://name and C:/files are filesystem paths. Single-letter
   prefixes are reserved for Windows drives, including C://files. */
static size_t scheme_length(const char *input)
{
    const char *cursor;
    if (!scheme_letter((unsigned char)input[0])) return 0;
    cursor = input + 1;
    while (scheme_letter((unsigned char)*cursor) ||
           (*cursor >= '0' && *cursor <= '9') ||
           *cursor == '+' || *cursor == '-' || *cursor == '.') cursor++;
    if (cursor == input + 1 || strncmp(cursor, "://", 3)) return 0;
    return (size_t)(cursor - input);
}

static bool handles_scheme(const ProviderType *type, const char *scheme,
                            size_t length)
{
    if (!type->schemes) return false;
    for (size_t index = 0; type->schemes[index]; index++)
        if (strlen(type->schemes[index]) == length &&
            !strncasecmp(type->schemes[index], scheme, length)) return true;
    return false;
}

static const ProviderType *find_type(const char *input, size_t length,
                                     char *error, size_t error_size)
{
    if (!length) return &provider_types[0];
    for (size_t index = 0; index < sizeof provider_types / sizeof provider_types[0]; index++)
        if (handles_scheme(&provider_types[index], input, length))
            return &provider_types[index];
    snprintf(error, error_size, "unsupported URI scheme: %.*s",
             (int)(length < 64 ? length : 64), input);
        return NULL;
    }

NavProvider *nav_provider_for_location(NavProvider *current, const char *input,
                                       char *error, size_t error_size)
{
    const ProviderType *type;
    size_t length;
    if (!input) {
        snprintf(error, error_size, "location is missing");
        return NULL;
    }
    length = scheme_length(input);
    type = find_type(input, length, error, error_size);
    if (!type) return NULL;
    if (!length) {
        bool drive = scheme_letter((unsigned char)input[0]) && input[1] == ':';
        if (current && !nav_path_is_absolute(input) && !drive &&
            strncmp(input, "\\\\", 2)) return current;
        return type->open_local();
    }
    if (current && current->scheme &&
        handles_scheme(type, current->scheme, strlen(current->scheme)))
        return current;
    snprintf(error, error_size,
             "%s URLs require a configured repository; open it from the repository menu",
             type->name);
    return NULL;
}

NavProvider *nav_provider_create_repository(const NavRepository *repository,
                                            NavCredentialStore *credentials,
                                            char *error, size_t error_size)
{
    const ProviderType *type;
    if (!repository) {
        snprintf(error, error_size, "repository is missing");
        return NULL;
    }
    type = find_type(repository->url, scheme_length(repository->url),
                     error, error_size);
    if (!type) return NULL;
    if (!type->create_repository) {
        snprintf(error, error_size, "%s does not support configured repositories",
                 type->name);
        return NULL;
    }
    return type->create_repository(repository, credentials, error, error_size);
}

/* Selection uses metadata only; opening a provider or resolving secrets is unnecessary. */
bool nav_provider_supports_credential_type(const char *url, NavCredentialType credential)
{
    char error[128];
    const ProviderType *type = url ? find_type(url, scheme_length(url), error, sizeof error) : NULL;
    return type && type->supports_credential && type->supports_credential(credential);
}

