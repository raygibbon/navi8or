/* Transient locations reuse repository providers and their authentication. */
#include "nav.h"
#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int canonical_url(const char *input, char *out, size_t size)
{
    CURLU *url = curl_url();
    char *text = NULL, *scheme = NULL, *user = NULL, *host = NULL;
    int result = -1;
    if (!url) return -1;
    if (curl_url_set(url, CURLUPART_URL, input, 0) ||
        curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) ||
        (strcasecmp(scheme, "http") && strcasecmp(scheme, "https"))) goto done;
    if (curl_url_get(url, CURLUPART_HOST, &host, 0)) goto done;
    for (char *c = host; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (curl_url_set(url, CURLUPART_HOST, host, 0) ||
        curl_url_set(url, CURLUPART_SCHEME, !strcasecmp(scheme, "https") ? "https" : "http", 0)) goto done;
    /* Embedded credentials are not a substitute for the vault. */
    if (!curl_url_get(url, CURLUPART_USER, &user, 0)) goto done;
    curl_url_set(url, CURLUPART_FRAGMENT, NULL, 0);
    if (curl_url_get(url, CURLUPART_URL, &text, CURLU_NO_DEFAULT_PORT) ||
        strlen(text) >= size) goto done;
    snprintf(out, size, "%s", text);
    result = 0;
done:
    curl_free(text); curl_free(scheme); curl_free(user); curl_free(host); curl_url_cleanup(url);
    return result;
}

/* Trailing slash is an explicit directory intent. No extension guessing or
   speculative GET of a possibly huge resource is needed. */
NavProvider *nav_location_resolve(NavApp *app, const char *input,
                                 NavEntry *entry, bool *directory, bool *owned,
                                 char *error, size_t size)
{
    NavProvider *provider;
    *owned = false;
    memset(entry, 0, sizeof *entry);
    if (strncasecmp(input, "http://", 7) && strncasecmp(input, "https://", 8)) {
        if (!strncasecmp(input, "smb://", 6)) {
            NavProvider *selected = NULL; size_t longest = 0;
            for (size_t i = 0; i < app->config.repository_count; i++) {
                const NavRepository *candidate = &app->config.repositories[i];
                size_t length = strlen(candidate->url);
                if (strncasecmp(candidate->url, "smb://", 6) || length <= longest ||
                    strncmp(input, candidate->url, length) ||
                    (candidate->url[length - 1] != '/' && input[length] && input[length] != '/')) continue;
                NavProvider *check = nav_smb_provider_create(candidate, app->credential_store, error, size);
                NavLocation location;
                if (check && !check->location(check, input, &location, error, size)) {
                    nav_provider_destroy(selected); selected = check; longest = length;
                } else nav_provider_destroy(check);
            }
            if (selected) { *owned = true; *directory = true; return selected; }
        }
        provider = nav_provider_for_location(app->panes[app->active].provider,
                                            input, error, size);
        *directory = true;
        if (provider && !strcmp(provider->scheme, "local")) {
            NavLocation resolved;
            if (!provider->location(provider, input, &resolved, error, size) &&
                !provider->stat(provider, resolved.resource_id, entry, error, size))
                *directory = (entry->flags & NAV_ENTRY_DIR) != 0;
        }
        return provider;
    }
    char url[NAV_URL_MAX];
    if (canonical_url(input, url, sizeof url)) {
        snprintf(error, size, "Invalid HTTP URL (use vault credentials, not URL passwords)");
        return NULL;
    }
    NavRepository repository = {.tls_verify = true};
    size_t best = 0;
    for (size_t i = 0; i < app->config.repository_count; i++) {
        const NavRepository *candidate = &app->config.repositories[i];
        char root[NAV_URL_MAX];
        if (canonical_url(candidate->url, root, sizeof root)) continue;
        char *query = strchr(root, '?');
        if (query) *query = 0;
        size_t length = strlen(root);
        if (length && root[length - 1] != '/') {
            if (length + 1 >= sizeof root) continue;
            root[length++] = '/'; root[length] = 0;
        }
        if (length > best && !strncmp(url, root, length)) {
            /* Ask the existing provider to enforce encoded traversal rules. */
            NavRepository normalized = *candidate;
            snprintf(normalized.url, sizeof normalized.url, "%s", root);
            NavProvider *check = nav_http_provider_create(&normalized, NULL, error, size);
            NavLocation location;
            char plain[NAV_URL_MAX]; snprintf(plain, sizeof plain, "%s", url);
            query = strchr(plain, '?'); if (query) *query = 0;
            bool valid = check && !check->location(check, plain, &location, error, size);
            nav_provider_destroy(check);
            if (valid) { repository = normalized; best = length; }
        }
    }
    if (!best) {
        /* Limit anonymous resources to their origin, allowing redirects there. */
        char *path = strchr(strstr(url, "://") + 3, '/');
        size_t length = path ? (size_t)(path - url) : strlen(url);
        snprintf(repository.url, sizeof repository.url, "%.*s/", (int)length, url);
        snprintf(repository.name, sizeof repository.name, "Transient HTTP");
    }
    provider = nav_http_provider_create(&repository, app->credential_store, error, size);
    if (!provider) return NULL;
    nav_http_provider_configure(provider, &app->config);
    *owned = true;
    snprintf(entry->resource_id, sizeof entry->resource_id, "%s", url);
    char path[NAV_URL_MAX]; snprintf(path, sizeof path, "%s", url);
    char *query = strchr(path, '?'); if (query) *query = 0;
    size_t length = strlen(path);
    *directory = length && path[length - 1] == '/';
    if (*directory) entry->flags |= NAV_ENTRY_DIR;
    const char *leaf = strrchr(path, '/');
    int decoded_length = 0;
    char *decoded = curl_easy_unescape(NULL, leaf ? leaf + 1 : path, 0, &decoded_length);
    if (!decoded || (size_t)decoded_length != strlen(decoded) || strpbrk(decoded, "/\\:") ||
        !strcmp(decoded, ".") || !strcmp(decoded, "..") ||
        strcspn(decoded, "\r\n\t") != strlen(decoded) || !nav_leaf_name_copy(entry->name, decoded))
        snprintf(entry->name, sizeof entry->name, "download");
    curl_free(decoded);
    return provider;
}
