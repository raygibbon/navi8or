#include "nav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void expect(int result, bool success, const char *operation, const char *error)
{
    if ((result == 0) != success) { fprintf(stderr, "%s: unexpected result: %s\n", operation, error); abort(); }
}
int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    NavConfig config = {0}; char error[256], resource[NAV_URL_MAX], upload_url[NAV_URL_MAX];
    NavCredentialStore *store = NULL; char vault[] = "/tmp/nav-network-vault-XXXXXX";
    if (argc == 3) {
        int fd = mkstemp(vault); assert(fd >= 0); close(fd); unlink(vault);
        assert(!nav_credential_store_create_vault(&store, vault, "network-master", error, sizeof error));
        NavCredential item = {0}; snprintf(item.name, sizeof item.name, "network-auth");
        item.type = !strcmp(argv[2], "basic") ? NAV_CREDENTIAL_BASIC : NAV_CREDENTIAL_BEARER;
        if (item.type == NAV_CREDENTIAL_BASIC) snprintf(item.username, sizeof item.username, "network-user");
        assert(!nav_credential_store_put(store, &item, "network-pass", false, error, sizeof error));
    }
    NavRepository repository = {.tls_verify = false, .writable = true};
    snprintf(repository.name, sizeof repository.name, "Network fixture");
    snprintf(repository.url, sizeof repository.url, "%sroot/", argv[1]);
    if (store) snprintf(repository.credential, sizeof repository.credential, "network-auth");
    config.repositories[0] = repository; config.repository_count = 1;
    snprintf(resource, sizeof resource, "%sroot/proxy-range.txt", argv[1]);
    snprintf(upload_url, sizeof upload_url, "%supload/proxy.txt", argv[1]);
    NavProvider *http = nav_http_provider_create(&repository, store, error, sizeof error);
    assert(http); nav_http_provider_configure(http, &config);
    snprintf(repository.url, sizeof repository.url, "%supload/", argv[1]);
    NavProvider *upload = nav_http_provider_create(&repository, store, error, sizeof error);
    assert(upload); nav_http_provider_configure(upload, &config);
    char target[] = "/tmp/nav-network-download-XXXXXX", source[] = "/tmp/nav-network-source-XXXXXX";
    int fd = mkstemp(target); assert(fd >= 0); close(fd); unlink(target);
    fd = mkstemp(source); assert(fd >= 0); assert(write(fd, "proxy body\n", 11) == 11); close(fd);
    const char *proxy = getenv("HTTPS_PROXY"); assert(proxy && proxy[0]); char *original = strdup(proxy); assert(original);
    NavApp *app = calloc(1, sizeof *app); assert(app); app->credential_store = store;
    for (int i = 0; i < 4; i++) {
        bool success = i % 2;
        config.proxy_mode = success ? NAV_PROXY_NONE : NAV_PROXY_SYSTEM;
        app->config = config;
        NavListing listing = {0}; NavEntry metadata;
        char root[NAV_URL_MAX]; snprintf(root, sizeof root, "%sroot/", argv[1]);
        expect(http->list(http, root, false, &listing, error, sizeof error), success, "list", error);
        nav_listing_free(&listing);
        expect(http->stat(http, resource, &metadata, error, sizeof error), success, "stat", error);
        unsigned char buffer[128]; size_t got = 0; bool eof, known; uint64_t total;
        expect(http->read_at(http, resource, 0, buffer, sizeof buffer, &got, &eof, &total, &known, error, sizeof error), success, "random read", error);
        void *reader = NULL;
        int result = http->open_read(http, resource, &reader, error, sizeof error);
        if (!result) result = http->read(http, reader, buffer, sizeof buffer, &got, error, sizeof error);
        expect(result, success, "stream read", error);
        if (reader) http->close(http, reader, error, sizeof error);
        expect(nav_download_copy(http, resource, nav_local_provider(), target, true, 65536, NULL, NULL, NULL, error, sizeof error), success, "download", error);
        expect(nav_transfer_copy(nav_local_provider(), source, upload, upload_url, true, NULL, NULL, error, sizeof error), success, "upload", error);
        NavEntry entry; bool directory, owned;
        NavProvider *direct = nav_location_resolve(app, resource, &entry, &directory, &owned, error, sizeof error);
        assert(direct && owned && !directory);
        expect(direct->stat(direct, entry.resource_id, &metadata, error, sizeof error), success, "direct URL stat", error);
        expect(nav_download_copy(direct, entry.resource_id, nav_local_provider(), target, true, 65536, NULL, NULL, NULL, error, sizeof error), success, "direct URL download", error);
        nav_provider_destroy(direct);
        assert(!strcmp(original, getenv("HTTPS_PROXY")));
    }
    unlink(target); unlink(source); free(original); nav_provider_destroy(http); nav_provider_destroy(upload);
    free(app);
    if (store) { nav_credential_store_close(store); unlink(vault); }
    puts("Network policy: System/None/System/None on existing providers, list/stat/read/range/upload/download: passed");
    return 0;
}
