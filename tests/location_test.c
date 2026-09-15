#include "nav.h"
#include "nav_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/resource.h>
#endif
static uint64_t transferred;
static bool cancelled, stalled;
static time_t cancel_deadline;
static void progress(uint64_t done, uint64_t total, bool known, void *data)
{ (void)total; (void)known; (void)data; transferred = done; }
static bool cancel(void *data)
{ (void)data; return (cancelled && transferred >= 65536) || (stalled && time(NULL) >= cancel_deadline); }
int main(int argc, char **argv)
{
    assert(argc == 5);
    const char *mode = argv[1], *url = argv[2], *target = argv[3], *base = argv[4];
    char error[512] = {0};
    NavApp *app = calloc(1, sizeof *app); assert(app); nav_config_defaults(&app->config);
    app->panes[0].provider = nav_local_provider();
    NavEntry entry; bool directory, owned;
    NavProvider *provider = nav_location_resolve(app, base, &entry, &directory, &owned, error, sizeof error);
    assert(provider == nav_local_provider() && directory && !owned);
    NavRepository smb_repo = {0};
    snprintf(smb_repo.name, sizeof smb_repo.name, "SMB fixture");
    snprintf(smb_repo.url, sizeof smb_repo.url, "smb://fixture/share/");
    app->config.repositories[0] = smb_repo; app->config.repository_count = 1;
    provider = nav_location_resolve(app, "smb://fixture/share/logs/", &entry, &directory, &owned, error, sizeof error);
    assert(provider && !strcmp(provider->scheme, "smb") && directory && owned);
    NavLocation smb_location; assert(!provider->location(provider, "smb://fixture/share/logs/", &smb_location, error, sizeof error));
    nav_provider_destroy(provider); app->config.repository_count = 0;
    if (!strcmp(mode, "auth")) {
        char vault[NAV_PATH_MAX]; snprintf(vault, sizeof vault, "%s/test.vault", base);
        assert(!nav_credential_store_create_vault(&app->credential_store, vault, "test password", error, sizeof error));
        const char *roots[] = {"/auth/", "/auth/nested/"};
        const char *names[] = {"broad", "specific"};
        for (size_t i = 0; i < 2; i++) {
            NavCredential credential = {.type = NAV_CREDENTIAL_BEARER};
            snprintf(credential.name, sizeof credential.name, "%s", names[i]);
            assert(!nav_credential_store_put(app->credential_store, &credential, names[i], false, error, sizeof error));
            char origin[NAV_URL_MAX]; snprintf(origin, sizeof origin, "%s", url);
            char *path = strchr(strstr(origin, "://") + 3, '/'); assert(path); *path = 0;
            NavRepository *repo = &app->config.repositories[i];
            snprintf(repo->name, sizeof repo->name, "%s", names[i]);
            snprintf(repo->credential, sizeof repo->credential, "%s", names[i]);
            assert(snprintf(repo->url, sizeof repo->url, "%s%s", origin, roots[i]) < (int)sizeof repo->url);
            repo->tls_verify = false;
        }
        app->config.repository_count = 2;
    }
    provider = nav_location_resolve(app, url, &entry, &directory, &owned, error, sizeof error);
    assert(provider && owned);
    if (!strcmp(mode, "directory")) {
        assert(directory); NavListing listing = {0}; NavLocation location;
        assert(!provider->location(provider, url, &location, error, sizeof error));
        assert(!provider->list(provider, location.resource_id, true, &listing, error, sizeof error));
        assert(listing.count == 2 && !strcmp(listing.items[1].name, "file.log")); nav_listing_free(&listing);
    } else if (!strcmp(mode, "redirect-directory")) {
        assert(!directory); NavEntry metadata;
        assert(!provider->stat(provider, entry.resource_id, &metadata, error, sizeof error));
        assert(metadata.flags & NAV_ENTRY_DIR);
    } else {
        assert(!directory); cancelled = !strcmp(mode, "cancel");
        if (!strcmp(mode, "auth")) {
            /* Link View and Ctrl+L View both pass this resolved, longest-root
             * authenticated provider to the same bounded Viewer source. */
            bool binary = false;
            NavViewSource *view = nav_view_source_open_provider(provider, entry.resource_id, &binary, error, sizeof error);
            assert(view && !binary);
            size_t length = 0; const char *line;
            if (view->cursor_line) {
                NavViewCursor cursor = {0}; view->cursor_top(view, &cursor);
                line = view->cursor_line(view, &cursor, &length);
            } else line = view->line(view, 0, &length);
            assert(line && length && strstr(line, "direct URL log content"));
            view->close(view);
        }
        stalled = !strcmp(mode, "stall-cancel"); cancel_deadline = time(NULL) + 2;
        int result = nav_download_copy(provider, entry.resource_id, nav_local_provider(), target,
                                      !strcmp(mode, "overwrite-failure"), stalled ? 1048576 : 65536, progress, cancel, NULL, error, sizeof error);
        if (!strcmp(mode, "failure") || !strcmp(mode, "overwrite-failure") || !strcmp(mode, "partial-conflict") || cancelled || stalled) {
            assert(result == -1); assert(error[0]);
            if (stalled) assert(strstr(error, "cancelled") && transferred == 0);
            FILE *file = fopen(target, "rb");
            if (!strcmp(mode, "overwrite-failure")) {
                assert(file); char kept[5] = {0}; assert(fread(kept, 1, 4, file) == 4); assert(!strcmp(kept, "keep")); fclose(file);
            } else assert(!file);
            char partial[NAV_PATH_MAX]; snprintf(partial, sizeof partial, "%s.part", target);
            file = fopen(partial, "rb");
            if (!strcmp(mode, "partial-conflict")) {
                assert(file); char kept[5] = {0}; assert(fread(kept, 1, 4, file) == 4); assert(!strcmp(kept, "keep")); fclose(file);
            } else assert(!file);
        } else {
            if (result) fprintf(stderr, "%s\n", error);
            assert(!result && transferred > 0);
#ifndef _WIN32
            if (!strcmp(mode, "large")) {
                struct rusage usage; assert(!getrusage(RUSAGE_SELF, &usage));
                assert(transferred == 64u * 1024u * 1024u);
                assert(usage.ru_maxrss < 32 * 1024); /* 64 MiB body, <32 MiB process. */
            }
#endif
        }
    }
    nav_provider_destroy(provider); nav_credential_store_close(app->credential_store); free(app);
    puts("location/download case passed");
    return 0;
}
