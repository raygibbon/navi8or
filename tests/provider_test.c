#include "nav.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Factory doubles keep dispatch tests independent of curl and the network. */
static NavProvider local = {.scheme = "local"};
static NavProvider http = {.scheme = "http"};
static NavProvider smb = {.scheme = "smb"};
static const NavRepository *expected_repository;
static NavCredentialStore *expected_credentials;
static size_t http_creations;
static size_t smb_creations;

NavProvider *nav_local_provider(void)
{
    return &local;
}

NavProvider *nav_http_provider_create(const NavRepository *repository,
                                      NavCredentialStore *credentials,
                                      char *error, size_t error_size)
{
    (void)error; (void)error_size;
    assert(repository == expected_repository);
    assert(credentials == expected_credentials);
    http_creations++;
    return &http;
}

NavProvider *nav_smb_provider_create(const NavRepository *repository,
                                     NavCredentialStore *credentials,
                                     char *error, size_t error_size)
{
    (void)error; (void)error_size;
    assert(repository == expected_repository);
    assert(credentials == expected_credentials);
    smb_creations++;
    return &smb;
}

int main(void)
{
    static const char *const paths[] = {
        "relative/path", "/absolute/path", "report:2026", "./http://name",
        "/tmp/https://name", "C:\\files", "C:/files", "C:files", "C://files",
        "\\\\server\\share", "", ".", "..",
    };
    static const char *const urls[] = {
        "http://example.test/root/", "https://example.test/root/",
        "HTTP://example.test/root/", "HtTpS://example.test/root/",
    };
    static const char *const unknown[] = {
        "sftp://server/root", "webdav://server/root",
        "custom+v2.0-test://server/root", "file:///tmp",
    };
    char error[256];
    for (size_t index = 0; index < sizeof paths / sizeof paths[0]; index++)
        assert(nav_provider_for_location(NULL, paths[index], error,
                                          sizeof error) == &local);
    for (size_t index = 0; index < sizeof urls / sizeof urls[0]; index++) {
        assert(!nav_provider_for_location(NULL, urls[index], error, sizeof error));
        assert(strstr(error, "configured repository"));
        assert(nav_provider_for_location(&http, urls[index], error,
                                          sizeof error) == &http);
        assert(!nav_provider_for_location(&local, urls[index], error, sizeof error));
    }
    for (size_t index = 0; index < sizeof unknown / sizeof unknown[0]; index++) {
        assert(!nav_provider_for_location(NULL, unknown[index], error, sizeof error));
        assert(strstr(error, "unsupported URI scheme"));
        assert(!nav_provider_for_location(&http, unknown[index], error, sizeof error));
    }
    assert(http_creations == 0);
    assert(!nav_provider_for_location(NULL, "smb://server/share/path", error, sizeof error));
    assert(strstr(error, "SMB URLs require a configured repository"));
    assert(nav_provider_for_location(&smb, "smb://server/share/path", error, sizeof error) == &smb);
    assert(nav_provider_for_location(&smb, "SMB://server/share/path", error, sizeof error) == &smb);
    assert(!nav_provider_for_location(&http, "smb://server/share/path", error, sizeof error));
    assert(!nav_provider_for_location(&smb, urls[0], error, sizeof error));
    assert(nav_provider_for_location(&smb, "child", error, sizeof error) == &smb);
    assert(nav_provider_for_location(&smb, "/tmp", error, sizeof error) == &local);
    assert(smb_creations == 0);
    assert(!nav_provider_for_location(NULL, NULL, error, sizeof error));
    assert(nav_provider_for_location(&http, "child", error, sizeof error) == &http);
    assert(nav_provider_for_location(&http, "report:2026", error, sizeof error) == &http);
    assert(nav_provider_for_location(&http, "/tmp", error, sizeof error) == &local);
    assert(nav_provider_for_location(&http, "C:\\files", error, sizeof error) == &local);
    assert(nav_provider_for_location(&http, "\\\\server\\share", error, sizeof error) == &local);

    NavRepository repository = {.tls_verify = true, .writable = true};
    max_align_t credential_token;
    expected_repository = &repository;
    expected_credentials = (NavCredentialStore *)&credential_token;
    snprintf(repository.name, sizeof repository.name, "Configured repository");
    snprintf(repository.credential, sizeof repository.credential, "vault-reference");
    for (size_t index = 0; index < 2; index++) {
        snprintf(repository.url, sizeof repository.url, "%s", urls[index]);
        assert(nav_provider_create_repository(&repository, expected_credentials,
                                               error, sizeof error) == &http);
    }
    assert(http_creations == 2);
    snprintf(repository.url, sizeof repository.url, "smb://server/share/path");
    assert(nav_provider_create_repository(&repository, expected_credentials,
                                           error, sizeof error) == &smb);
    assert(smb_creations == 1 && http_creations == 2);
    snprintf(repository.url, sizeof repository.url, "unknown://server/share");
    assert(!nav_provider_create_repository(&repository, NULL, error, sizeof error));
    assert(strstr(error, "unsupported URI scheme: unknown"));
    snprintf(repository.url, sizeof repository.url, "/tmp");
    assert(!nav_provider_create_repository(&repository, NULL, error, sizeof error));
    assert(!nav_provider_create_repository(NULL, NULL, error, sizeof error));
    assert(http_creations == 2);
    assert(nav_provider_supports_credential_type("https://example.test/", NAV_CREDENTIAL_BASIC));
    assert(nav_provider_supports_credential_type("HTTP://example.test/", NAV_CREDENTIAL_BEARER));
    assert(!nav_provider_supports_credential_type("https://example.test/", (NavCredentialType)99));
    assert(!nav_provider_supports_credential_type("unknown://host/", NAV_CREDENTIAL_BASIC));
    assert(!nav_provider_supports_credential_type(NULL, NAV_CREDENTIAL_BASIC));
    puts("provider dispatch: ok");
    return 0;
}
