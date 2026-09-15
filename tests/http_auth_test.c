#include "nav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void url(char *out, size_t size, const char *origin, const char *path)
{ assert(snprintf(out, size, "%s%s", origin, path) < (int)size); }
static void repository(NavRepository *repo, const char *origin, const char *path, const char *name)
{
    memset(repo, 0, sizeof *repo);
    snprintf(repo->name, sizeof repo->name, "%s", name);
    url(repo->url, sizeof repo->url, origin, path);
    snprintf(repo->credential, sizeof repo->credential, "%s", name);
}
static void seed(NavApp *app, const char *directory, const char *origin)
{
    char path[NAV_PATH_MAX], error[256] = {0};
    url(path, sizeof path, directory, "/vault.bin");
    assert(!nav_credential_store_create_vault(&app->credential_store, path, "auth-test-master", error, sizeof error));
    const char *names[] = {"Wrong HTTP", "Work HTTP", "Work Bearer"};
    for (size_t i = 0; i < 3; i++) {
        NavCredential credential = {.type = i == 2 ? NAV_CREDENTIAL_BEARER : NAV_CREDENTIAL_BASIC};
        snprintf(credential.name, sizeof credential.name, "%s", names[i]);
        if (i != 2) snprintf(credential.username, sizeof credential.username, "fixture-user");
        assert(!nav_credential_store_put(app->credential_store, &credential,
            i ? "fixture-pass" : "fixture-wrong", false, error, sizeof error));
    }
    repository(&app->config.repositories[0], origin, "/files/", "Work HTTP");
    repository(&app->config.repositories[1], origin, "/known/", "Work HTTP");
    snprintf(app->config.repositories[1].name, sizeof app->config.repositories[1].name, "Known logs");
    /* Explicit anonymous TLS fixture: production defaults still verify TLS. */
    repository(&app->config.repositories[2], origin, "/", "Anonymous TLS fixture");
    app->config.repositories[2].credential[0] = 0;
    app->config.repository_count = 3;
    app->config.explicit_config = true;
    url(app->config.config_path, sizeof app->config.config_path, directory, "/nav.toml");
    assert(!nav_config_save_repositories(&app->config, error, sizeof error));
}
static void checks(NavApp *app, const char *directory, const char *origin)
{
    char error[256] = {0}, target[NAV_URL_MAX], root[NAV_URL_MAX];
    seed(app, directory, origin);
    NavEntry entry, metadata; bool dir, owned;
    url(target, sizeof target, origin, "/known/job.log");
    NavProvider *p = nav_location_resolve(app, target, &entry, &dir, &owned, error, sizeof error);
    assert(p && !strcmp(nav_http_credential_name(p), "Work HTTP"));
    assert(!p->stat(p, target, &metadata, error, sizeof error));
    assert(!nav_http_authentication_needed(p));
    NavProvider *explicit = nav_http_authentication_retry_provider(p, app->credential_store,
        target, "Wrong HTTP", NULL, error, sizeof error);
    assert(explicit && explicit->stat(explicit, target, &metadata, error, sizeof error));
    assert(nav_http_authentication_needed(explicit));
    assert(!strcmp(nav_http_credential_name(p), "Work HTTP"));
    nav_provider_destroy(explicit); nav_provider_destroy(p);
    const char *paths[] = {"/", "/logs/", "/logs/private/"};
    const char *names[] = {"Wrong HTTP", "Work HTTP", "Work Bearer"};
    for (size_t i = 0; i < 3; i++) repository(&app->config.auth_scopes[i], origin, paths[i], names[i]);
    app->config.auth_scope_count = 3;
    url(target, sizeof target, origin, "/logs/private/job.log");
    p = nav_location_resolve(app, target, &entry, &dir, &owned, error, sizeof error);
    assert(p && !strcmp(nav_http_credential_name(p), "Work Bearer")); nav_provider_destroy(p);
    /* Repository precedence beats a longer credential rule. */
    repository(&app->config.auth_scopes[3], origin, "/known/private/", "Wrong HTTP");
    app->config.auth_scope_count = 4;
    url(target, sizeof target, origin, "/known/private/job.log");
    p = nav_location_resolve(app, target, &entry, &dir, &owned, error, sizeof error);
    assert(p && !strcmp(nav_http_credential_name(p), "Work HTTP")); nav_provider_destroy(p);
    app->config.auth_scope_count = 0;
    url(target, sizeof target, origin, "/logs/job.log");
    assert(!nav_http_scope_suggest(target, root, sizeof root));
    char expected[NAV_URL_MAX]; url(expected, sizeof expected, origin, "/logs/"); assert(!strcmp(root, expected));
    assert(!nav_http_scope_normalize(root, target, expected, sizeof expected, error, sizeof error));
    const char *bad[] = {"https://elsewhere.invalid/logs/", "http://localhost/logs/", "/logs/", "https://localhost:1/logs/"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        assert(nav_http_scope_normalize(bad[i], target, expected, sizeof expected, error, sizeof error));
    const char *bad_paths[] = {"/files/", "/logs/%2e%2e/", "/logs/%00/", "/logs/%5c/", "/logs/?q=1", "/logs/#fragment"};
    for (size_t i = 0; i < sizeof bad_paths / sizeof *bad_paths; i++) {
        url(expected, sizeof expected, origin, bad_paths[i]);
        assert(nav_http_scope_normalize(expected, target, root, sizeof root, error, sizeof error));
    }
    p = nav_location_resolve(app, target, &entry, &dir, &owned, error, sizeof error);
    assert(p && !nav_http_credential_name(p)[0]);
    assert(p->stat(p, target, &metadata, error, sizeof error));
    assert(nav_http_authentication_needed(p) && nav_http_authentication_types(p) == 1);
    NavProvider *retry = nav_http_authentication_retry_provider(p, app->credential_store, target, "Work HTTP", NULL, error, sizeof error);
    assert(retry && !retry->stat(retry, target, &metadata, error, sizeof error));
    assert(!nav_http_credential_name(p)[0]);
    char other[NAV_URL_MAX]; url(other, sizeof other, origin, "/logs/other.log");
    assert(retry->stat(retry, other, &metadata, error, sizeof error));
    nav_provider_destroy(retry); nav_provider_destroy(p);
    assert(!app->config.auth_scope_count);
    NavRepository scope; repository(&scope, origin, "/logs/", "Work HTTP");
    assert(!nav_http_auth_scope_remember(&app->config, &scope, target, error, sizeof error));
    assert(app->config.auth_scope_count == 1);
    assert(!nav_http_auth_scope_remember(&app->config, &scope, target, error, sizeof error));
    assert(app->config.auth_scope_count == 1);  /* idempotent update */
    NavConfig reloaded;
    assert(!nav_config_load_file(&reloaded, app->config.config_path, error, sizeof error));
    assert(reloaded.auth_scope_count == 1 && !strcmp(reloaded.auth_scopes[0].credential, "Work HTTP"));
    app->config = reloaded;
    p = nav_location_resolve(app, other, &entry, &dir, &owned, error, sizeof error);
    assert(p && !p->stat(p, other, &metadata, error, sizeof error)); nav_provider_destroy(p);
    url(target, sizeof target, origin, "/forbidden/job.log");
    p = nav_location_resolve(app, target, &entry, &dir, &owned, error, sizeof error);
    assert(p && p->stat(p, target, &metadata, error, sizeof error));
    assert(strstr(error, "403") && !nav_http_authentication_needed(p)); nav_provider_destroy(p);
    /* HEAD and Range must reject redirects before sending scoped credentials. */
    url(target, sizeof target, origin, "/files/redirect-path");
    p = nav_location_resolve(app, target, &entry, &dir, &owned, error, sizeof error);
    assert(p && p->stat(p, target, &metadata, error, sizeof error));
    unsigned char buffer[64]; size_t got; bool eof, known; uint64_t total;
    assert(p->read_at(p, target, 0, buffer, sizeof buffer, &got, &eof, &total, &known, error, sizeof error));
    nav_provider_destroy(p);
    url(target, sizeof target, origin, "/files/redirect-once");
    p = nav_location_resolve(app, target, &entry, &dir, &owned, error, sizeof error);
    assert(p);
    retry = nav_http_authentication_retry_provider(p, app->credential_store, target,
        "Work HTTP", NULL, error, sizeof error);
    assert(retry && retry->stat(retry, target, &metadata, error, sizeof error));
    assert(!nav_http_authentication_needed(retry));
    nav_provider_destroy(retry); nav_provider_destroy(p);
    url(target, sizeof target, origin, "/files/redirect-host");
    p = nav_location_resolve(app, target, &entry, &dir, &owned, error, sizeof error);
    assert(p && p->stat(p, target, &metadata, error, sizeof error)); nav_provider_destroy(p);
    puts("HTTP auth resolution, scopes, once isolation, challenges and redirects passed");
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    NavApp *app = calloc(1, sizeof *app); assert(app);
    nav_config_defaults(&app->config);
    app->panes[0].provider = app->panes[1].provider = nav_local_provider();
    if (!strcmp(argv[1], "seed")) seed(app, argv[2], argv[3]);
    else checks(app, argv[2], argv[3]);
    nav_credential_store_close(app->credential_store); free(app); return 0;
}
