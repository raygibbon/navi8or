#include "nav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Test-only hooks use the production curl callback and allocation paths. */
void nav_http_test_listing_allocations(size_t);
int nav_http_test_parse_chunks(NavProvider *, const char *, const char *, size_t,
                              size_t, NavListing *, char *, size_t);

static void parse(NavProvider *provider, const char *url, const char *html,
                  size_t chunk, size_t expected)
{
    NavListing listing = {0};
    char error[256] = {0};
    assert(nav_http_test_parse_chunks(provider, url, html, strlen(html), chunk,
                                     &listing, error, sizeof error) == 0);
    assert(listing.count == expected);
    assert(!strcmp(listing.items[0].name, ".."));
    assert(listing.items[0].flags & NAV_ENTRY_PARENT);
    nav_listing_free(&listing);
}

static void metadata_cases(NavProvider *provider, const char *url)
{
    static const struct { const char *tail; unsigned flags; uint64_t size; } cases[] = {
        {"", 0, 0}, {" 123", NAV_ENTRY_SIZE_KNOWN, 123},
        {" 14-Sep-2026 18:45", NAV_ENTRY_MODIFIED_KNOWN, 0},
        {" 14-Sep-2026 18:45 123", NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN, 123},
        {" 14-Sep-2026 18:45 -", NAV_ENTRY_MODIFIED_KNOWN, 0},
        {" 14-Sep-2026 18:45 oops", NAV_ENTRY_MODIFIED_KNOWN, 0},
        {" 31-Feb-2026 18:45 123", NAV_ENTRY_SIZE_KNOWN, 123},
        {" 14-Xxx-2026 99:45 123", NAV_ENTRY_SIZE_KNOWN, 123},
        {" -", 0, 0}, {" 0", NAV_ENTRY_SIZE_KNOWN, 0},
        {" 18446744073709551615", NAV_ENTRY_SIZE_KNOWN, UINT64_MAX},
        {" 18446744073709551616", 0, 0}, {" -123", 0, 0}, {" 1.2K", 0, 0},
    };
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        char html[512];
        snprintf(html, sizeof html, "<a href='foo.txt'>123 misleading anchor label</a>%s\n", cases[index].tail);
        for (size_t chunk = 1; chunk <= strlen(html); chunk++) {
            NavListing listing = {0};
            char error[256] = {0};
            assert(nav_http_test_parse_chunks(provider, url, html, strlen(html), chunk,
                                             &listing, error, sizeof error) == 0);
            assert(listing.count == 2);
            NavEntry *entry = &listing.items[1];
            assert((entry->flags & (NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN)) == cases[index].flags);
            if (entry->flags & NAV_ENTRY_SIZE_KNOWN) assert(entry->size == cases[index].size);
            if (entry->flags & NAV_ENTRY_MODIFIED_KNOWN) {
                struct tm *tm = localtime(&entry->modified);
                assert(tm && tm->tm_year == 126 && tm->tm_mon == 8 && tm->tm_mday == 14);
                assert(tm->tm_hour == 18 && tm->tm_min == 45);
                char row[100], size[32];
                nav_format_entry_full(entry, 90, row, sizeof row);
                assert(strstr(row, "2026-09-14 18:45"));
                if (entry->flags & NAV_ENTRY_SIZE_KNOWN) {
                    nav_format_size(entry->size, size, sizeof size);
                    assert(strstr(row, size));
                }
            }
            nav_listing_free(&listing);
        }
    }
    NavListing listing = {0};
    char error[256] = {0};
    const char *html = "<a href='sub/'>sub/</a> 14-Sep-2026 18:45 -\n"
        "<a href='foo.txt'>foo</a>\n<a href='foo.txt'>duplicate</a> 14-Sep-2026 18:45 123";
    assert(nav_http_test_parse_chunks(provider, url, html, strlen(html), 1,
                                     &listing, error, sizeof error) == 0);
    assert(listing.count == 3 && (listing.items[1].flags & NAV_ENTRY_DIR));
    assert((listing.items[1].flags & NAV_ENTRY_MODIFIED_KNOWN) && !(listing.items[1].flags & NAV_ENTRY_SIZE_KNOWN));
    assert(listing.items[2].size == 123 && (listing.items[2].flags & NAV_ENTRY_SIZE_KNOWN));
    nav_listing_free(&listing);
    char long_row[17000];
    memset(long_row, ' ', sizeof long_row);
    memcpy(long_row, "<a href='foo.txt'>foo</a>", 25);
    long_row[sizeof long_row - 1] = 0;
    assert(nav_http_test_parse_chunks(provider, url, long_row, strlen(long_row), 7,
                                     &listing, error, sizeof error) == -1);
    assert(strstr(error, "row/tag") && !listing.items);
}

int main(int argc, char **argv)
{
    NavRepository repository = {.name = "listing", .tls_verify = false};
    NavListing listing = {0};
    char error[256] = {0};
    snprintf(repository.url, sizeof repository.url, "%s",
             argc > 1 ? argv[1] : "http://localhost/root/");
    NavProvider *provider = nav_http_provider_create(&repository, NULL, error, sizeof error);
    assert(provider);
    if (argc > 1) {
        const char *mode = argv[2];
        if (!strcmp(mode, "stat")) {
            for (int index = 0; index < 3; index++) {
                char url[NAV_URL_MAX + 32];
                snprintf(url, sizeof url, "%sfile%d.txt", repository.url, index);
                NavEntry entry;
                assert(provider->stat(provider, url, &entry, error, sizeof error) == 0);
                assert(entry.size == 123 && (entry.flags & NAV_ENTRY_SIZE_KNOWN));
                assert(!!(entry.flags & NAV_ENTRY_MODIFIED_KNOWN) == (index == 0));
                if (!index) assert(entry.modified == (time_t)1789411500);
            }
            nav_provider_destroy(provider);
            return 0;
        }
        if (!strcmp(mode, "oom")) nav_http_test_listing_allocations(2);
        int result = provider->list(provider, repository.url, false, &listing,
                                    error, sizeof error);
        if (!strcmp(mode, "long") || !strcmp(mode, "oom") || !strcmp(mode, "outside")) {
            assert(result == -1 && listing.count == 0);
            assert(strstr(error, !strcmp(mode, "long") ? "16 KiB" :
                                !strcmp(mode, "oom") ? "out of memory" : "repository root"));
        } else {
            if (result) fprintf(stderr, "%s\n", error);
            assert(result == 0);
            size_t expected = !strcmp(mode, "large") ? 30001 :
                              !strcmp(mode, "empty") ? 1 : 3;
            assert(listing.count == expected);
            assert(!strcmp(listing.items[0].name, ".."));
            if (expected > 1)
                assert(!strcmp(listing.items[1].name, !strcmp(mode, "large") ? "file00000.txt" : "foo.txt"));
            if (!strcmp(mode, "large")) {
                for (size_t index = 1; index < listing.count; index++) {
                    assert(listing.items[index].size == UINT64_MAX);
                    assert((listing.items[index].flags & (NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN)) ==
                           (NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN));
                }
            }
            if (!strcmp(mode, "redirect")) {
                char resolved[NAV_URL_MAX + 16];
                snprintf(resolved, sizeof resolved, "%ssub/foo.txt", repository.url);
                assert(!strcmp(listing.items[1].resource_id, resolved));
            }
        }
        nav_listing_free(&listing);
        nav_provider_destroy(provider);
        return 0;
    }
    metadata_cases(provider, repository.url);
    const char *html = "<html><A class='x>y' hrEF = \"foo.txt\">foo</A>"
        "<a href='sub/'>sub</a><a href=foo.txt>duplicate</a>"
        "<a href='./foo.txt'>resolved duplicate</a><a href='../'>parent</a>"
        "<a href='?sort=name'>query</a><a href='foo.txt#x'>fragment</a>"
        "<a href='/outside/escape.txt'>outside</a><a href='https://other/root/foo'>other</a>"
        "<a href='bad%2Fname'>invalid</a><a href='bad%5Cname'>invalid</a>"
        "<abbr href='wrong.txt'>not anchor</abbr><a href='incomplete.txt'";
    for (size_t chunk = 1; chunk <= strlen(html); chunk++) parse(provider, repository.url, html, chunk, 3);
    parse(provider, repository.url, "<a hr" "ef=\"foo.txt\">", 5, 2);
    parse(provider, repository.url, "<a href='space%20name.txt'><a href='unfinished", 1, 2);
    parse(provider, repository.url, "<broken <a href='foo.txt'>", 1, 2);
    parse(provider, repository.url, "plain text <a href=\"unterminated>", 3, 1);
    parse(provider, repository.url,
          "<!-- author's <a href='ignored.txt'> --><a href='foo.txt'><!-- incomplete", 1, 2);
    assert(nav_http_parse_directory_html(provider, repository.url, html, &listing,
                                         error, sizeof error) == 0 && listing.count == 3);
    assert(!strcmp(listing.items[1].name, "foo.txt"));
    assert(!strcmp(listing.items[2].name, "sub") && (listing.items[2].flags & NAV_ENTRY_DIR));
    nav_listing_free(&listing);

    assert(nav_http_parse_directory_html(provider, repository.url,
        "<a href='space%20name.txt'><a href='%2E%2E'><a href='%2E'>"
        "<a href='bad%00name'><a href='bad%0Aname'>",
        &listing, error, sizeof error) == 0 && listing.count == 2);
    assert(!strcmp(listing.items[1].name, "space name.txt"));
    nav_listing_free(&listing);

    char *large = malloc(6 * 1024 * 1024 + 1);
    assert(large);
    memset(large, 'x', 6 * 1024 * 1024);
    memcpy(large + 6 * 1024 * 1024 - 18, "<a href='foo.txt'>", 18);
    large[6 * 1024 * 1024] = 0;
    parse(provider, repository.url, large, 7, 2);
    free(large);

    char long_tag[17000];
    memset(long_tag, 'x', sizeof long_tag);
    memcpy(long_tag, "<a data='", 9);
    long_tag[sizeof long_tag - 1] = 0;
    assert(nav_http_test_parse_chunks(provider, repository.url, long_tag, strlen(long_tag),
                                     13, &listing, error, sizeof error) == -1);
    assert(strstr(error, "16 KiB") && !listing.items);

    char bounded_tag[16385];
    memset(bounded_tag, 'x', sizeof bounded_tag);
    memcpy(bounded_tag, "<a data='", 9);
    const char *tail = "' href='foo.txt'>";
    memcpy(bounded_tag + 16384 - strlen(tail), tail, strlen(tail));
    bounded_tag[16384] = 0;
    parse(provider, repository.url, bounded_tag, 1, 2);

    /* Parent allocation, hash allocation, and entry-array/index growth failures. */
    char many[12000];
    size_t length = 0;
    for (size_t index = 0; index < 150; index++)
        length += (size_t)snprintf(many + length, sizeof many - length,
                                  "<a href='file%zu.txt'>", index);
    for (size_t budget = 0; budget < 8; budget++) {
        nav_http_test_listing_allocations(budget);
        assert(nav_http_test_parse_chunks(provider, repository.url, many, length, 3,
                                         &listing, error, sizeof error) == -1);
        assert(strstr(error, "out of memory") && !listing.items && !listing.count);
    }
    nav_http_test_listing_allocations(SIZE_MAX);
    parse(provider, repository.url, many, 3, 151);
    nav_provider_destroy(provider);
    puts("HTTP incremental listing parser: ok");
    return 0;
}
