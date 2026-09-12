#include "nav.h"
#include "nav_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { bool known; uint64_t done, total; } Progress;
typedef struct { size_t offset; } UnknownRead;
static int unknown_open(NavProvider *provider, const char *path, void **handle,
                        char *error, size_t error_size)
{
    (void)provider; (void)path; (void)error; (void)error_size;
    *handle = calloc(1, sizeof(UnknownRead));
    return *handle ? 0 : -1;
}
static int unknown_read(NavProvider *provider, void *handle, void *buffer,
                        size_t capacity, size_t *got, char *error,
                        size_t error_size)
{
    static const char data[] = "unknown-size upload body";
    UnknownRead *read = handle;
    size_t remaining = sizeof data - 1 - read->offset;
    (void)provider; (void)error; (void)error_size;
    *got = remaining < capacity ? remaining : capacity;
    memcpy(buffer, data + read->offset, *got);
    read->offset += *got;
    return 0;
}
static int unknown_close(NavProvider *provider, void *handle, char *error,
                         size_t error_size)
{
    (void)provider; (void)error; (void)error_size; free(handle); return 0;
}
static NavProvider unknown_provider = {
    .scheme = "unknown", .capabilities = NAV_CAP_READ,
    .open_read = unknown_open, .read = unknown_read, .close = unknown_close,
};
static size_t range_reads;
static int (*real_read_at)(NavProvider *, const char *, uint64_t, void *, size_t,
                           size_t *, bool *, uint64_t *, bool *, char *, size_t);
static int counted_read_at(NavProvider *provider, const char *resource_id,
                           uint64_t offset, void *buffer, size_t capacity,
                           size_t *got, bool *eof, uint64_t *total,
                           bool *total_known, char *error, size_t error_size)
{
    range_reads++;
    return real_read_at(provider, resource_id, offset, buffer, capacity, got,
                        eof, total, total_known, error, error_size);
}
static void progress(uint64_t done, uint64_t total, bool known, void *data)
{
    Progress *progress = data;
    progress->known = known;
    progress->done = done;
    progress->total = total;
}

static void verify_pattern(const char *path, size_t expected, unsigned factor)
{
    FILE *file = fopen(path, "rb");
    size_t offset = 0;
    assert(file);
    for (;;) {
        unsigned char buffer[8192];
        size_t got = fread(buffer, 1, sizeof buffer, file);
        for (size_t index = 0; index < got; index++, offset++)
            assert(buffer[index] == (unsigned char)((offset * factor) & 255u));
        if (got < sizeof buffer) { assert(!ferror(file)); break; }
    }
    assert(offset == expected);
    fclose(file);
}

static void write_bytes(const char *path, const void *data, size_t length)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, length, file) == length && fclose(file) == 0);
}

static void write_zero_file(const char *path, size_t length)
{
    unsigned char block[65536] = {0};
    FILE *file = fopen(path, "wb");
    assert(file);
    while (length) {
        size_t amount = length < sizeof block ? length : sizeof block;
        assert(fwrite(block, 1, amount, file) == amount);
        length -= amount;
    }
    assert(fclose(file) == 0);
}

static const NavEntry *find_entry(const NavListing *listing, const char *name)
{
    for (size_t index = 0; index < listing->count; index++)
        if (!strcmp(listing->items[index].name, name)) return &listing->items[index];
    return NULL;
}

static void test_stream_redirects(NavProvider *provider, const char *root)
{
    static const struct {
        const char *path;
        const char *failure;
    } cases[] = {
        {"direct", NULL}, {"absolute", NULL}, {"relative", NULL},
        {"status301", NULL}, {"status303", NULL}, {"status307", NULL},
        {"status308", NULL}, {"limit/5", NULL},
        {"outside", "repository root"}, {"cross", "repository root"},
        {"traversal", "repository root"}, {"encoded", "repository root"},
        {"encoded-slash", "repository root"},
        {"chain", "repository root"}, {"limit/6", "redirect"},
        {"missing", "404"}, {"denied", "403"}, {"unauthorized", "401"},
    };
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        char url[NAV_URL_MAX], error[256] = {0};
        void *handle = NULL;
        size_t total = 0;
        snprintf(url, sizeof url, "%s%s", root, cases[index].path);
        assert(provider->open_read(provider, url, &handle, error,
                                   sizeof error) == 0);
        for (;;) {
            unsigned char buffer[37];
            size_t got = 999;
            memset(buffer, 0xa5, sizeof buffer);
            int result = provider->read(provider, handle, buffer, sizeof buffer,
                                        &got, error, sizeof error);
            if (cases[index].failure) {
                assert(result != 0 && got == 0 && total == 0);
                assert(strstr(error, cases[index].failure));
                /* A failure must not even modify the supplied buffer. */
                for (size_t byte = 0; byte < sizeof buffer; byte++)
                    assert(buffer[byte] == 0xa5);
                assert(provider->read(provider, handle, buffer, sizeof buffer,
                                       &got, error, sizeof error) != 0);
                assert(got == 0);
                break;
            }
            if (result) fprintf(stderr, "stream %s after %zu bytes: %s\n",
                                cases[index].path, total, error);
            assert(result == 0);
            for (size_t byte = 0; byte < got; byte++) assert(buffer[byte] == 'S');
            total += got;
            if (!got) { assert(total == 128 * 1024); break; }
        }
        assert(provider->close(provider, handle, error, sizeof error) == 0);
    }
}

int main(int argc, char **argv)
{
    NavRepository repository = {.tls_verify = false};
    NavProvider *provider;
    NavCredentialStore *credential_store = NULL;
    char vault_path[] = "/tmp/navi8or-http-vault-XXXXXX";
    NavPane pane = {0};
    NavListing parsed = {0};
    NavLocation child, parent, destination_location;
    char error[256] = {0};
    bool expect_success;
    static const char html[] =
        "<!doctype html><A class='x' HREF=\"sub/\">sub</A>"
        "<a href='space%20name.txt'>space</a>"
        "<a href=\"/root/absolute.txt\">absolute</a>"
        "<a href='?C=N;O=D'>sort</a><a href='../'>parent</a>"
        "<a href='../../escape.txt'>escape</a>"
        "<a href='https://evil.example/file'>external</a>";
    if (argc != 5 && argc != 7 && argc != 8 && argc != 9) return 2;
    snprintf(repository.name, sizeof repository.name, "HTTP Test");
    if (nav_repository_normalize_url(argv[1], repository.url,
                                     sizeof repository.url,
                                     error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 2;
    }
    repository.tls_verify = !strcmp(argv[2], "1");
    expect_success = !strcmp(argv[3], "1");
    if (argc == 9 && !strcmp(argv[5], "cross")) {
        NavCredential item = {.type = NAV_CREDENTIAL_BASIC};
        NavRepository second = repository;
        NavProvider *first_provider, *second_provider;
        NavListing first_listing = {0}, second_listing = {0};
        int descriptor = mkstemp(vault_path);
        assert(descriptor >= 0 && close(descriptor) == 0 &&
               unlink(vault_path) == 0);
        assert(nav_credential_store_create_vault(&credential_store, vault_path,
                                                  "test-master", error,
                                                  sizeof error) == 0);
        snprintf(item.name, sizeof item.name, "local-basic");
        snprintf(item.username, sizeof item.username, "navi8or");
        assert(nav_credential_store_put(credential_store, &item, argv[6], false,
                                        error, sizeof error) == 0);
        memset(&item, 0, sizeof item);
        item.type = NAV_CREDENTIAL_BEARER;
        snprintf(item.name, sizeof item.name, "local-bearer");
        assert(nav_credential_store_put(credential_store, &item, argv[8], false,
                                        error, sizeof error) == 0);
        snprintf(repository.credential, sizeof repository.credential,
                 "local-basic");
        snprintf(second.name, sizeof second.name, "Second HTTP Test");
        assert(nav_repository_normalize_url(argv[7], second.url,
                                            sizeof second.url, error,
                                            sizeof error) == 0);
        snprintf(second.credential, sizeof second.credential, "local-bearer");
        first_provider = nav_http_provider_create(&repository, credential_store,
                                                  error, sizeof error);
        second_provider = nav_http_provider_create(&second, credential_store,
                                                   error, sizeof error);
        assert(first_provider && second_provider);
        assert(first_provider->list(first_provider, repository.url, false,
                                    &first_listing, error, sizeof error) == 0);
        assert(second_provider->list(second_provider, second.url, false,
                                     &second_listing, error, sizeof error) == 0);
        nav_listing_free(&first_listing);
        nav_listing_free(&second_listing);
        nav_provider_destroy(first_provider);
        nav_provider_destroy(second_provider);
        nav_credential_store_close(credential_store);
        assert(unlink(vault_path) == 0);
        return 0;
    }
    if (argc >= 7 && strcmp(argv[5], "none")) {
        NavCredential item = {0};
        int descriptor = mkstemp(vault_path);
        assert(descriptor >= 0 && close(descriptor) == 0 &&
               unlink(vault_path) == 0);
        assert(nav_credential_store_create_vault(&credential_store, vault_path,
                                                  "test-master", error,
                                                  sizeof error) == 0);
        snprintf(item.name, sizeof item.name, "%s",
                 !strcmp(argv[5], "bearer") ? "local-bearer" : "local-basic");
        item.type = !strcmp(argv[5], "bearer")
                        ? NAV_CREDENTIAL_BEARER : NAV_CREDENTIAL_BASIC;
        if (item.type == NAV_CREDENTIAL_BASIC)
            snprintf(item.username, sizeof item.username, "navi8or");
        assert(nav_credential_store_put(credential_store, &item, argv[6], false,
                                        error, sizeof error) == 0);
        snprintf(item.name, sizeof item.name, "deleted");
        assert(nav_credential_store_put(credential_store, &item, "discarded",
                                        false, error, sizeof error) == 0);
        assert(nav_credential_store_delete(credential_store, "deleted", error,
                                           sizeof error) == 0);
        snprintf(repository.credential, sizeof repository.credential,
                 "%s", !strcmp(argv[5], "bearer")
                           ? "local-bearer" : "local-basic");
        {
            NavRepository insecure = repository;
            NavRepository missing = repository;
            NavProvider *rejected;
            char insecure_url[NAV_URL_MAX];
            const char *authority = strstr(repository.url, "://");
            assert(authority && snprintf(insecure_url, sizeof insecure_url,
                                         "http%s", authority) <
                                (int)sizeof insecure_url);
            snprintf(insecure.url, sizeof insecure.url, "%s", insecure_url);
            rejected = nav_http_provider_create(&insecure, credential_store,
                                                error, sizeof error);
            assert(!rejected && strstr(error, "require HTTPS"));
            snprintf(missing.credential, sizeof missing.credential, "deleted");
            rejected = nav_http_provider_create(&missing, credential_store,
                                                error, sizeof error);
            assert(rejected);
            {
                NavListing listing = {0};
                assert(rejected->list(rejected, missing.url, false, &listing,
                                      error, sizeof error) != 0);
                assert(strstr(error, "not found"));
                nav_listing_free(&listing);
            }
            nav_provider_destroy(rejected);
            nav_credential_store_lock(credential_store);
            rejected = nav_http_provider_create(&repository, credential_store,
                                                error, sizeof error);
            assert(rejected);
            {
                NavListing listing = {0};
                assert(rejected->list(rejected, repository.url, false, &listing,
                                      error, sizeof error) != 0);
                assert(strstr(error, "locked"));
                nav_listing_free(&listing);
            }
            nav_provider_destroy(rejected);
            assert(nav_credential_store_unlock(credential_store, "test-master",
                                               error, sizeof error) == 0);
        }
    }
    provider = nav_http_provider_create(&repository, credential_store,
                                        error, sizeof error);
    assert(provider);
    if (argc == 8 && !strcmp(argv[7], "stream-redirects")) {
        test_stream_redirects(provider, repository.url);
        nav_provider_destroy(provider);
        if (credential_store) {
            nav_credential_store_close(credential_store);
            assert(unlink(vault_path) == 0);
        }
        return 0;
    }
    if (argc == 8 && !strcmp(argv[7], "redirect")) {
        NavListing listing = {0};
        assert(provider->list(provider, repository.url, false, &listing,
                              error, sizeof error) != 0);
        if (!strstr(error, "redirect left repository root"))
            fprintf(stderr, "redirect probe error: %s\n", error);
        assert(strstr(error, "redirect left repository root"));
        nav_listing_free(&listing);
        nav_provider_destroy(provider);
        nav_credential_store_close(credential_store);
        assert(unlink(vault_path) == 0);
        return 0;
    }
    assert(nav_provider_supports(provider, NAV_CAP_LIST | NAV_CAP_READ |
                                 NAV_CAP_STAT | NAV_CAP_RANDOM_READ));
    assert(provider->open_read && provider->read && provider->close &&
           provider->read_at);
    real_read_at = provider->read_at;
    provider->read_at = counted_read_at;
    assert(!provider->open_write && !provider->remove &&
           !provider->mkdir && !provider->rename_path);
    assert(nav_http_parse_directory_html(provider, repository.url, html,
                                         &parsed, error, sizeof error) == 0);
    assert(find_entry(&parsed, "sub") &&
           (find_entry(&parsed, "sub")->flags & NAV_ENTRY_DIR));
    assert(find_entry(&parsed, "space name.txt"));
    assert(find_entry(&parsed, "absolute.txt"));
    assert(!find_entry(&parsed, "escape.txt"));
    assert(parsed.count == 4);
    nav_listing_free(&parsed);
    pane.provider = provider;
    pane.history.current = -1;
    pane.history_enabled = true;
    pane.history.limit = NAV_HISTORY_MAX;
    pane.view = NAV_PANEL_FULL;
    if (!expect_success) {
        assert(nav_pane_open(&pane, repository.url, false, true,
                             error, sizeof error) != 0);
        if (repository.credential[0]) {
            assert(strstr(error, "authentication failed"));
            nav_listing_free(&pane.listing);
            nav_provider_destroy(provider);
            nav_credential_store_close(credential_store);
            assert(unlink(vault_path) == 0);
            return 0;
        }
        assert(strstr(error, "TLS verification failed"));
        {
            NavRepository writable = repository;
            NavProvider *upload, *local = nav_local_provider();
            NavLocation root, target;
            char upload_url[NAV_URL_MAX], path[] = "/tmp/nav-tls-upload-XXXXXX";
            const char *root_part = strstr(repository.url, "/root/");
            int descriptor;
            assert(root_part && snprintf(upload_url, sizeof upload_url,
                                         "%.*s/upload/",
                                         (int)(root_part - repository.url),
                                         repository.url) < (int)sizeof upload_url);
            writable.writable = true;
            snprintf(writable.url, sizeof writable.url, "%s", upload_url);
            upload = nav_http_provider_create(&writable, credential_store,
                                              error, sizeof error);
            assert(upload && upload->location(upload, upload_url, &root, error,
                                              sizeof error) == 0);
            assert(upload->location_child(upload, &root, "tls.bin", &target,
                                          error, sizeof error) == 0);
            descriptor = mkstemp(path);
            assert(descriptor >= 0 && write(descriptor, "tls", 3) == 3 &&
                   close(descriptor) == 0);
            assert(nav_transfer_copy(local, path, upload, target.resource_id,
                                     false, NULL, NULL, error,
                                     sizeof error) != 0);
            assert(strstr(error, "certificate") || strstr(error, "SSL"));
            assert(unlink(path) == 0);
            nav_provider_destroy(upload);
        }
        nav_listing_free(&pane.listing);
        nav_provider_destroy(provider);
        nav_credential_store_close(credential_store);
        if (credential_store) assert(unlink(vault_path) == 0);
        return 0;
    }
    assert(nav_pane_open(&pane, repository.url, false, true,
                         error, sizeof error) == 0);
    assert(find_entry(&pane.listing, "sub"));
    assert(find_entry(&pane.listing, "space name.txt"));
    assert(!find_entry(&pane.listing, "external"));
    assert(provider->location(provider, find_entry(&pane.listing, "sub")->resource_id,
                              &child, error, sizeof error) == 0);
    assert(nav_pane_load(&pane, &child, false, true, error, sizeof error) == 0);
    assert(find_entry(&pane.listing, "nested.txt"));
    assert(provider->location_parent(provider, &pane.location, &parent,
                                     error, sizeof error) == 0);
    assert(!strcmp(parent.resource_id, repository.url));
    assert(nav_pane_load(&pane, &parent, false, true, error, sizeof error) == 0);
    assert(nav_pane_refresh(&pane, false, error, sizeof error) == 0);
    {
        const NavEntry *large = find_entry(&pane.listing, "large.txt");
        const NavEntry *small_fallback = find_entry(&pane.listing, "fallback-small.txt");
        const NavEntry *large_fallback = find_entry(&pane.listing, "fallback-large.txt");
        const NavEntry *long_line = find_entry(&pane.listing, "long-line.txt");
        const NavEntry *aligned = find_entry(&pane.listing, "aligned.txt");
        const NavEntry *partial = find_entry(&pane.listing, "partial.txt");
        const NavEntry *utf8 = find_entry(&pane.listing, "utf8-boundary.txt");
        const NavEntry *limit_minus = find_entry(&pane.listing, "line-limit-minus.txt");
        const NavEntry *limit_exact = find_entry(&pane.listing, "line-limit.txt");
        const NavEntry *limit_plus = find_entry(&pane.listing, "line-limit-plus.txt");
        NavViewSource *source;
        NavViewer viewer;
        bool binary = false;
        size_t length = 0;
        assert(large && small_fallback && large_fallback && long_line && aligned &&
               partial && utf8 && limit_minus && limit_exact && limit_plus);
        source = nav_view_source_open_provider(provider, large->resource_id,
                                               &binary, error, sizeof error);
        assert(source && !binary && source->cursor_line && source->cursor_move);
        nav_viewer_init(&viewer, source);
        assert(source->cursor_line(source, &viewer.cursor, &length) && length > 0);
        nav_viewer_goto_line(&viewer, 20002, 20);
        assert(viewer.cursor.offset > 1024u * 1024u);
        assert(source->cursor_line(source, &viewer.cursor, &length) &&
               strstr(source->cursor_line(source, &viewer.cursor, &length),
                      "line 00020001"));
        {
            size_t before = range_reads;
            nav_viewer_page(&viewer, -1, 20);
            assert(range_reads == before);
        }
        nav_viewer_bottom(&viewer, 20);
        assert(source->cursor_line(source, &viewer.cursor, &length) &&
               strstr(source->cursor_line(source, &viewer.cursor, &length),
                      "line 00819199"));
        snprintf(viewer.search, sizeof viewer.search, "line 00819100");
        assert(nav_viewer_find(&viewer, -1, 20, NULL));
        snprintf(viewer.search, sizeof viewer.search, "MARKER-5MB");
        nav_viewer_top(&viewer);
        assert(nav_viewer_find(&viewer, 1, 20, NULL));
        assert(viewer.cursor.offset >= 5u * 1024u * 1024u);
        {
            size_t before = range_reads;
            nav_viewer_top(&viewer);
            assert(source->cursor_line(source, &viewer.cursor, &length));
            assert(range_reads == before + 1);
        }
        source->close(source);
        source = nav_view_source_open_provider(provider, long_line->resource_id,
                                               &binary, error, sizeof error);
        assert(source && !binary);
        nav_viewer_init(&viewer, source);
        assert(source->cursor_line(source, &viewer.cursor, &length) &&
               length == 2u * 65536u + 17u);
        nav_viewer_bottom(&viewer, 20);
        assert(source->cursor_line(source, &viewer.cursor, &length) &&
               length == strlen("last-no-newline"));
        source->close(source);
        source = nav_view_source_open_provider(provider, aligned->resource_id,
                                               &binary, error, sizeof error);
        assert(source && !binary);
        nav_viewer_init(&viewer, source);
        nav_viewer_bottom(&viewer, 20);
        assert(source->cursor_line(source, &viewer.cursor, &length));
        source->close(source);
        source = nav_view_source_open_provider(provider, partial->resource_id,
                                               &binary, error, sizeof error);
        assert(source && !binary);
        nav_viewer_init(&viewer, source);
        nav_viewer_bottom(&viewer, 20);
        assert(source->cursor_line(source, &viewer.cursor, &length));
        source->close(source);
        {
            unsigned char byte;
            size_t got = 0;
            bool eof = false, total_known = false;
            uint64_t total = 0;
            assert(provider->read_at(provider, aligned->resource_id, 65536,
                                     &byte, 1, &got, &eof, &total,
                                     &total_known, error, sizeof error) != 0);
            assert(strstr(error, "416"));
        }
        source = nav_view_source_open_provider(provider,
                                               small_fallback->resource_id,
                                               &binary, error, sizeof error);
        assert(source && !binary);
        source->close(source);
        source = nav_view_source_open_provider(provider, utf8->resource_id,
                                               &binary, error, sizeof error);
        assert(source && !binary);
        nav_viewer_init(&viewer, source);
        {
            const char *line = source->cursor_line(source, &viewer.cursor,
                                                    &length);
            assert(line && length == 65538 &&
                   !memcmp(line + 65535, "\xe2\x82\xac", 3));
        }
        source->close(source);
        source = nav_view_source_open_provider(provider, limit_minus->resource_id,
                                               &binary, error, sizeof error);
        assert(source && !binary);
        nav_viewer_init(&viewer, source);
        assert(source->cursor_line(source, &viewer.cursor, &length) &&
               length == 8u * 1024u * 1024u - 1u);
        source->close(source);
        source = nav_view_source_open_provider(provider, limit_exact->resource_id,
                                               &binary, error, sizeof error);
        assert(source && !binary);
        nav_viewer_init(&viewer, source);
        assert(source->cursor_line(source, &viewer.cursor, &length) &&
               length == 8u * 1024u * 1024u);
        source->close(source);
        source = nav_view_source_open_provider(provider, limit_plus->resource_id,
                                               &binary, error, sizeof error);
        assert(source && !binary);
        nav_viewer_init(&viewer, source);
        assert(!source->cursor_line(source, &viewer.cursor, &length));
        assert(source->last_error &&
               strstr(source->last_error(source), "Line exceeds remote Viewer limit"));
        source->close(source);
        source = nav_view_source_open_provider(provider,
                                               large_fallback->resource_id,
                                               &binary, error, sizeof error);
        assert(!source && strstr(error, "bounded Range"));
    }
    {
        char directory[] = "/tmp/navi8or-http-copy-XXXXXX";
        char known_path[NAV_PATH_MAX], unknown_path[NAV_PATH_MAX], failed_path[NAV_PATH_MAX];
        NavProvider *local = nav_local_provider();
        Progress known = {0}, unknown = {0};
        const NavEntry *known_entry = find_entry(&pane.listing, "binary.bin");
        const NavEntry *unknown_entry = find_entry(&pane.listing, "unknown.bin");
        const NavEntry *failed_entry = find_entry(&pane.listing, "interrupted.bin");
        assert(known_entry && unknown_entry && failed_entry && mkdtemp(directory));
        assert(local->location(local, directory, &destination_location,
                               error, sizeof error) == 0);
        assert(local->location_child(local, &destination_location, "binary.bin",
                                     &child, error, sizeof error) == 0);
        snprintf(known_path, sizeof known_path, "%s", child.resource_id);
        assert(nav_transfer_copy_with_buffer(provider, known_entry->resource_id,
                                             local, known_path, false, 32768,
                                             progress, &known, error,
                                             sizeof error) == 0);
        assert(known.known && known.done == 5u * 1024u * 1024u &&
               known.total == known.done);
        verify_pattern(known_path, 5u * 1024u * 1024u, 31);
        assert(local->location_child(local, &destination_location, "unknown.bin",
                                     &child, error, sizeof error) == 0);
        snprintf(unknown_path, sizeof unknown_path, "%s", child.resource_id);
        if (nav_transfer_copy_with_buffer(provider, unknown_entry->resource_id,
                                          local, unknown_path, false, 32768,
                                          progress, &unknown, error,
                                          sizeof error) != 0) {
            fprintf(stderr, "unknown-length transfer failed: %s\n", error);
            assert(0);
        }
        assert(!unknown.known && unknown.done == 384u * 1024u);
        verify_pattern(unknown_path, 384u * 1024u, 17);
        assert(local->location_child(local, &destination_location, "failed.bin",
                                     &child, error, sizeof error) == 0);
        snprintf(failed_path, sizeof failed_path, "%s", child.resource_id);
        assert(nav_transfer_copy_with_buffer(provider, failed_entry->resource_id,
                                             local, failed_path, false, 32768,
                                             NULL, NULL, error, sizeof error) != 0);
        assert(access(failed_path, F_OK) != 0);
        assert(unlink(known_path) == 0 && unlink(unknown_path) == 0 &&
               rmdir(directory) == 0);
    }
    {
        NavRepository writable = repository;
        NavProvider *upload;
        NavProvider *local = nav_local_provider();
        NavLocation upload_root, target;
        NavPane uploaded = {0};
        Progress uploaded_progress = {0};
        char upload_url[NAV_URL_MAX], directory[] = "/tmp/navi8or-http-upload-XXXXXX";
        char zero_path[NAV_PATH_MAX], tiny_path[NAV_PATH_MAX];
        char special_path[NAV_PATH_MAX], large_path[NAV_PATH_MAX];
        char interrupted_path[NAV_PATH_MAX];
        char mutation_source[NAV_PATH_MAX], mutation_target[NAV_PATH_MAX];
        const char *root_part = strstr(repository.url, "/root/");
        assert(root_part && mkdtemp(directory));
        assert(snprintf(upload_url, sizeof upload_url, "%.*s/upload/",
                        (int)(root_part - repository.url), repository.url) <
               (int)sizeof upload_url);
        snprintf(writable.name, sizeof writable.name, "Writable HTTP Test");
        snprintf(writable.url, sizeof writable.url, "%s", upload_url);
        writable.writable = true;
        writable.mkdir_enabled = true;
        writable.delete_enabled = true;
        writable.rename_enabled = true;
        upload = nav_http_provider_create(&writable, credential_store,
                                          error, sizeof error);
        assert(upload && nav_provider_supports(upload, NAV_CAP_WRITE |
                                                NAV_CAP_MKDIR | NAV_CAP_DELETE |
                                                NAV_CAP_RENAME) &&
               upload->open_write && upload->write);
        assert(upload->remove && upload->mkdir && upload->rename_path);
        assert(upload->location(upload, upload_url, &upload_root, error,
                                sizeof error) == 0);
        assert(upload->location_child(upload, &upload_root, "../escape",
                                      &target, error, sizeof error) != 0);
        assert(upload->location_child(upload, &upload_root, "..", &target,
                                      error, sizeof error) != 0);
        assert(upload->location_child(upload, &upload_root, "bad\nname", &target,
                                      error, sizeof error) != 0);

        snprintf(zero_path, sizeof zero_path, "%s/zero.bin", directory);
        write_bytes(zero_path, "", 0);
        assert(upload->location_child(upload, &upload_root, "zero.bin", &target,
                                      error, sizeof error) == 0);
        assert(nav_transfer_copy(local, zero_path, upload, target.resource_id,
                                 false, progress, &uploaded_progress, error,
                                 sizeof error) == 0);
        assert(uploaded_progress.known && uploaded_progress.done == 0 &&
               uploaded_progress.total == 0);

        snprintf(tiny_path, sizeof tiny_path, "%s/tiny.txt", directory);
        write_bytes(tiny_path, "first upload\n", 13);
        assert(upload->location_child(upload, &upload_root, "tiny.txt", &target,
                                      error, sizeof error) == 0);
        assert(nav_transfer_copy(local, tiny_path, upload, target.resource_id,
                                 false, NULL, NULL, error, sizeof error) == 0);
        write_bytes(tiny_path, "second upload\n", 14);
        assert(nav_transfer_copy(local, tiny_path, upload, target.resource_id,
                                 false, NULL, NULL, error, sizeof error) != 0);
        assert(strstr(error, "already exists"));
        assert(nav_transfer_copy(local, tiny_path, upload, target.resource_id,
                                 true, NULL, NULL, error, sizeof error) == 0);

        snprintf(special_path, sizeof special_path,
                 "%s/space # %% \xc3\xbc (x).txt", directory);
        write_bytes(special_path, "special\n", 8);
        assert(upload->location_child(upload, &upload_root,
                                      "space # % \xc3\xbc (x).txt", &target,
                                      error, sizeof error) == 0);
        assert(strstr(target.resource_id, "space%20%23%20%25%20%C3%BC%20%28x%29.txt"));
        assert(nav_transfer_copy(local, special_path, upload, target.resource_id,
                                 false, NULL, NULL, error, sizeof error) == 0);

        assert(upload->location_child(upload, &upload_root, "unknown.bin", &target,
                                      error, sizeof error) == 0);
        assert(nav_transfer_copy(&unknown_provider, "synthetic", upload,
                                 target.resource_id, false, NULL, NULL, error,
                                 sizeof error) == 0);

        snprintf(interrupted_path, sizeof interrupted_path, "%s/interrupted.bin",
                 directory);
        write_zero_file(interrupted_path, 16u * 1024u * 1024u);
        assert(upload->location_child(upload, &upload_root,
                                      "interrupted-upload.bin", &target,
                                      error, sizeof error) == 0);
        if (nav_transfer_copy(local, interrupted_path, upload,
                              target.resource_id, false, NULL, NULL, error,
                              sizeof error) == 0) {
            fprintf(stderr, "interrupted upload unexpectedly succeeded: %s\n",
                    error);
            assert(0);
        }
        assert(strstr(error, "partial file"));
        assert(access(interrupted_path, F_OK) == 0);

        /* The failed PUT must not leave provider or curl state behind. */
        assert(upload->location_child(upload, &upload_root, "after-failure.txt",
                                      &target, error, sizeof error) == 0);
        assert(nav_transfer_copy(local, tiny_path, upload, target.resource_id,
                                 false, NULL, NULL, error, sizeof error) == 0);

        const char *failure_names[] = {"forbidden.bin", "unsupported.bin",
                                       "too-large.bin"};
        const char *failure_text[] = {"denied", "does not support upload",
                                      "too large"};
        for (size_t index = 0; index < 3; index++) {
            assert(upload->location_child(upload, &upload_root,
                                          failure_names[index], &target,
                                          error, sizeof error) == 0);
            assert(nav_transfer_copy(local, tiny_path, upload,
                                     target.resource_id, false, NULL, NULL,
                                     error, sizeof error) != 0);
            if (!strstr(error, failure_text[index]))
                fprintf(stderr, "%s upload error: %s\n", failure_names[index],
                        error);
            assert(strstr(error, failure_text[index]));
        }

        if (!strcmp(argv[4], "1")) {
            snprintf(large_path, sizeof large_path, "%s/large.bin", directory);
            write_zero_file(large_path, 100u * 1024u * 1024u);
            assert(upload->location_child(upload, &upload_root, "large.bin",
                                          &target, error, sizeof error) == 0);
            uploaded_progress = (Progress){0};
            assert(nav_transfer_copy(local, large_path, upload,
                                     target.resource_id, false, progress,
                                     &uploaded_progress, error,
                                     sizeof error) == 0);
            assert(uploaded_progress.known &&
                   uploaded_progress.done == 100u * 1024u * 1024u);
            assert(unlink(large_path) == 0);
        }

        uploaded.provider = upload;
        uploaded.history.current = -1;
        uploaded.history_enabled = true;
        uploaded.history.limit = NAV_HISTORY_MAX;
        uploaded.view = NAV_PANEL_FULL;
        assert(nav_pane_open(&uploaded, upload_url, false, true, error,
                             sizeof error) == 0);
        assert(find_entry(&uploaded.listing, "tiny.txt") &&
               find_entry(&uploaded.listing, "space # % \xc3\xbc (x).txt"));
        assert(upload->mkdir(upload, upload_root.resource_id, error,
                             sizeof error) != 0);
        assert(upload->remove(upload, upload_root.resource_id, error,
                              sizeof error) != 0);
        assert(upload->location_child(upload, &upload_root,
                                      "dir # % \xc3\xbc (x)", &target,
                                      error, sizeof error) == 0);
        assert(upload->mkdir(upload, target.resource_id, error,
                             sizeof error) == 0);
        assert(nav_pane_refresh(&uploaded, false, error, sizeof error) == 0);
        assert(find_entry(&uploaded.listing, "dir # % \xc3\xbc (x)"));
        assert(upload->remove(upload, target.resource_id, error,
                              sizeof error) == 0);
        assert(upload->remove(upload, target.resource_id, error,
                              sizeof error) != 0 && strstr(error, "no longer exists"));
        assert(upload->location_child(upload, &upload_root, "delete-me.txt",
                                      &target, error, sizeof error) == 0);
        assert(nav_transfer_copy(local, tiny_path, upload, target.resource_id,
                                 false, NULL, NULL, error, sizeof error) == 0);
        assert(upload->remove(upload, target.resource_id, error,
                              sizeof error) == 0);

        assert(upload->location_child(upload, &upload_root, "rename-old.txt",
                                      &target, error, sizeof error) == 0);
        snprintf(mutation_source, sizeof mutation_source, "%s", target.resource_id);
        assert(nav_transfer_copy(local, tiny_path, upload, mutation_source,
                                 false, NULL, NULL, error, sizeof error) == 0);
        assert(upload->location_child(upload, &upload_root,
                                      "renamed # % \xc3\xbc (x).txt", &target,
                                      error, sizeof error) == 0);
        snprintf(mutation_target, sizeof mutation_target, "%s", target.resource_id);
        assert(upload->rename_path(upload, mutation_source, mutation_target,
                                   error, sizeof error) == 0);
        assert(upload->rename_path(upload, mutation_target,
                                   find_entry(&uploaded.listing, "tiny.txt")->resource_id,
                                   error, sizeof error) != 0 &&
               strstr(error, "already exists"));

        assert(upload->location_child(upload, &upload_root, "nonempty",
                                      &target, error, sizeof error) == 0);
        snprintf(mutation_source, sizeof mutation_source, "%s", target.resource_id);
        assert(upload->mkdir(upload, mutation_source, error, sizeof error) == 0);
        NavLocation mutation_directory;
        assert(upload->location(upload, mutation_source, &mutation_directory,
                                error, sizeof error) == 0);
        assert(upload->location_child(upload, &mutation_directory, "child.txt",
                                      &target, error, sizeof error) == 0);
        assert(nav_transfer_copy(local, tiny_path, upload, target.resource_id,
                                 false, NULL, NULL, error, sizeof error) == 0);
        assert(upload->remove(upload, mutation_source, error, sizeof error) != 0 &&
               strstr(error, "not empty"));
        assert(upload->location_child(upload, &upload_root, "nonempty-renamed",
                                      &target, error, sizeof error) == 0);
        snprintf(mutation_target, sizeof mutation_target, "%s", target.resource_id);
        assert(upload->rename_path(upload, mutation_source, mutation_target,
                                   error, sizeof error) == 0);
        assert(upload->rename_path(upload, upload_root.resource_id,
                                   mutation_target, error, sizeof error) != 0);
        nav_listing_free(&uploaded.listing);
        nav_provider_destroy(upload);
        assert(unlink(zero_path) == 0 && unlink(tiny_path) == 0 &&
               unlink(special_path) == 0 && unlink(interrupted_path) == 0 &&
               rmdir(directory) == 0);
    }
    nav_listing_free(&pane.listing);
    nav_provider_destroy(provider);
    nav_credential_store_close(credential_store);
    if (credential_store) assert(unlink(vault_path) == 0);
    return 0;
}
