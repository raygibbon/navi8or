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
        {" 18446744073709551616", 0, 0}, {" -123", 0, 0},
        {" 123 B", NAV_ENTRY_SIZE_KNOWN, 123},
        {" 0 B", NAV_ENTRY_SIZE_KNOWN, 0},
        {" 18446744073709551615 B", NAV_ENTRY_SIZE_KNOWN, UINT64_MAX},
#define APPROX (NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_SIZE_APPROXIMATE)
        {" 12K", APPROX, 12288}, {" 12KB", APPROX, 12288}, {" 12 KiB", APPROX, 12288},
        {" 12kb", APPROX, 12288}, {" 12 kIb", APPROX, 12288},
        {" 1.5M", APPROX, 1572864}, {" 1.5MB", APPROX, 1572864}, {" 1.5 MiB", APPROX, 1572864},
        {" 2G", APPROX, 2147483648ULL}, {" 2GB", APPROX, 2147483648ULL}, {" 2 GiB", APPROX, 2147483648ULL},
        {" 1T", APPROX, 1099511627776ULL}, {" 1TB", APPROX, 1099511627776ULL}, {" 1 TiB", APPROX, 1099511627776ULL},
        {" 1.2K", APPROX, 1228}, {" 0K", APPROX, 0},
        {" 14-Sep-2026 18:45 12 KiB", APPROX | NAV_ENTRY_MODIFIED_KNOWN, 12288},
        {" 2026-09-14 18:45 12 KiB", APPROX | NAV_ENTRY_MODIFIED_KNOWN, 12288},
        {" 18014398509481983K", APPROX, UINT64_MAX - 1023},
        {" 18014398509481984K", 0, 0}, {" 18446744073709551615T", 0, 0},
        {" 1XB", 0, 0}, {" 1KBjunk", 0, 0}, {" 1..5M", 0, 0}, {" 1.M", 0, 0},
        {" .5M", 0, 0}, {" 1e3K", 0, 0}, {" 1.5", 0, 0}, {" 123 B extra", NAV_ENTRY_SIZE_KNOWN, 123},
        {" 15-Sep-2026 14:30 14K text/plain", APPROX | NAV_ENTRY_MODIFIED_KNOWN, 14336},
        {" 15-Sep-2026 14:30 14K", APPROX | NAV_ENTRY_MODIFIED_KNOWN, 14336},
        {" 15-Sep-2026 14:30 123", NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN, 123},
        {"\t15-Sep-2026\t14:30\t14\tKiB\ttext/plain", APPROX | NAV_ENTRY_MODIFIED_KNOWN, 14336},
        {" 15-Sep-2026 14:30 14 KiB Description", APPROX | NAV_ENTRY_MODIFIED_KNOWN, 14336},
        {" 15-Sep-2026 14:30 - 123 misleading description", NAV_ENTRY_MODIFIED_KNOWN, 0},
        {" 15-Sep-2026 14:30 unknown 123", NAV_ENTRY_MODIFIED_KNOWN, 0},
        {"&nbsp;15-Sep-2026&#32;14:30&#x20;14KB&nbsp;text/plain", APPROX | NAV_ENTRY_MODIFIED_KNOWN, 14336},
#undef APPROX
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
            assert((entry->flags & (NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN | NAV_ENTRY_SIZE_APPROXIMATE)) == cases[index].flags);
            if (entry->flags & NAV_ENTRY_SIZE_APPROXIMATE) {
                char display[32], row[100]; nav_format_entry_size(entry, false, display, sizeof display);
                assert(display[0] == '~'); nav_format_entry_full(entry, 90, row, sizeof row);
                assert(strstr(row, display + 1) && !strchr(row, '~'));
            }
            if (entry->flags & NAV_ENTRY_SIZE_KNOWN) assert(entry->size == cases[index].size);
            if (entry->flags & NAV_ENTRY_MODIFIED_KNOWN) {
                struct tm *tm = localtime(&entry->modified);
                bool next_day = strstr(cases[index].tail, "15-Sep") != NULL;
                assert(tm && tm->tm_year == 126 && tm->tm_mon == 8 && tm->tm_mday == (next_day ? 15 : 14));
                assert(tm->tm_hour == (next_day ? 14 : 18) && tm->tm_min == (next_day ? 30 : 45));
                char row[100], size[32];
                nav_format_entry_full(entry, 90, row, sizeof row);
                assert(strstr(row, next_day ? "2026-09-15 14:30" : "2026-09-14 18:45"));
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
    const char *table = "<table><tr><td><a href='apache.bin'>apache.bin</a></td>"
        "<td>2026-09-14 18:45</td><td>1.5 MiB</td></tr></table>";
    assert(!nav_http_test_parse_chunks(provider, url, table, strlen(table), 1, &listing, error, sizeof error));
    assert(listing.count == 2 && listing.items[1].size == 1572864);
    assert((listing.items[1].flags & (NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_SIZE_APPROXIMATE | NAV_ENTRY_MODIFIED_KNOWN)) ==
           (NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_SIZE_APPROXIMATE | NAV_ENTRY_MODIFIED_KNOWN));
    nav_listing_free(&listing);
    const char *tables[] = {
        "<tr><td><a href='d'>d</a></td><td>15-Sep-2026 14:30</td><td>2.5M</td><td>log file</td></tr>",
        "<tr>\n<td><a href='d'>d</a></td>\n<td>15-Sep-2026&nbsp;14:30</td>\n<td>2.5 MiB</td><td></td></tr>",
        "<tr><td><a href='d'>d</a></td><td>15-Sep-2026 14:30</td><td>2.5M</td><td>123 &amp; description</td></tr>"
    };
    for (size_t i = 0; i < sizeof tables / sizeof tables[0]; i++) {
        for (size_t chunk = 1; chunk <= strlen(tables[i]); chunk++) {
            assert(!nav_http_test_parse_chunks(provider, url, tables[i], strlen(tables[i]), chunk, &listing, error, sizeof error));
            assert(listing.count == 2 && listing.items[1].size == 2621440);
            assert(listing.items[1].flags & NAV_ENTRY_SIZE_APPROXIMATE);
            nav_listing_free(&listing);
        }
    }
    const char *missing = "<tr><td><a href='empty'>empty</a></td>"
        "<td>15-Sep-2026 14:30</td><td></td><td>123 misleading description</td></tr>";
    assert(!nav_http_test_parse_chunks(provider, url, missing, strlen(missing), 1, &listing, error, sizeof error));
    assert(listing.count == 2 && !(listing.items[1].flags & NAV_ENTRY_SIZE_KNOWN));
    nav_listing_free(&listing);
    NavEntry enormous = {.size = UINT64_MAX, .flags = NAV_ENTRY_SIZE_KNOWN};
    NavConfig byte_config; nav_config_defaults(&byte_config); byte_config.size_bytes = true;
    char row[100], human[32]; nav_format_size(enormous.size, human, sizeof human);
    nav_format_entry_full_for_config(&enormous, 90, row, sizeof row, &byte_config);
    assert(strstr(row, human) && !strstr(row, "184467440737"));
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

typedef struct { size_t calls, increases, entries; uint64_t bytes, total; bool known, cancel; } ListReports;
static void report_listing(const NavListProgress *progress, void *data)
{
    ListReports *reports = data;
    reports->calls++;
    assert(progress->entries >= reports->entries && progress->bytes_received >= reports->bytes);
    if (progress->entries > reports->entries) reports->increases++;
    reports->entries = progress->entries; reports->bytes = progress->bytes_received;
    reports->known = progress->total_known; reports->total = progress->total_bytes;
}
static bool cancel_listing(void *data)
{ ListReports *reports = data; return reports->cancel && reports->entries >= 100; }
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
                if (!!(entry.flags & NAV_ENTRY_MODIFIED_KNOWN) != (index == 0))
                    fprintf(stderr, "stat file%d: flags=%u modified=%lld\n", index, entry.flags, (long long)entry.modified);
                assert(!!(entry.flags & NAV_ENTRY_MODIFIED_KNOWN) == (index == 0));
                if (!index) assert(entry.modified == (time_t)1789411500);
            }
            nav_provider_destroy(provider);
            return 0;
        }
        if (!strcmp(mode, "oom")) nav_http_test_listing_allocations(2);
        ListReports reports = {.cancel = !strcmp(mode, "cancel")};
        NavListOptions options = {.progress = report_listing, .cancel = cancel_listing, .userdata = &reports};
        NavPane *pane = calloc(1, sizeof *pane), *before = malloc(sizeof *before); assert(pane && before);
        pane->provider = provider; pane->history.current = -1;
        pane->listing.items = calloc(1, sizeof(NavEntry)); assert(pane->listing.items);
        pane->listing.count = pane->listing.capacity = 1;
        snprintf(pane->listing.items[0].name, NAV_NAME_MAX, "old pane");
        *before = *pane;
        int result = nav_pane_open_progress(pane, repository.url, false, true, &options, error, sizeof error);
        if (result) {
            assert(!memcmp(pane, before, sizeof *pane));
            assert(!strcmp(pane->listing.items[0].name, "old pane"));
        } else { listing = pane->listing; memset(&pane->listing, 0, sizeof pane->listing); }
        nav_listing_free(&pane->listing); free(pane); free(before);
        if (!strcmp(mode, "long") || !strcmp(mode, "oom") || !strcmp(mode, "outside") || !strcmp(mode, "cancel")) {
            assert(result == -1 && listing.count == 0);
            printf("%s listing rejected: %s\n", mode, error); fflush(stdout);
            assert(strstr(error, !strcmp(mode, "long") ? "16 KiB" :
                                !strcmp(mode, "oom") ? "out of memory" : !strcmp(mode, "cancel") ? "cancelled" : "repository root"));
        } else {
            if (result) fprintf(stderr, "%s\n", error);
            assert(result == 0);
            size_t expected = !strcmp(mode, "large") ? 30001 : !strcmp(mode, "known") ? 3001 :
                              !strcmp(mode, "empty") ? 1 : 3;
            assert(listing.count == expected);
            assert(!strcmp(listing.items[0].name, ".."));
            if (expected > 1)
                assert(!strcmp(listing.items[1].name, (!strcmp(mode, "large") || !strcmp(mode, "known")) ? "file00000.txt" : "foo.txt"));
            if (!strcmp(mode, "large") || !strcmp(mode, "known")) {
                const uint64_t sizes[] = {123, 14336, 2621440, 8589934592ULL, 0, 0, UINT64_MAX};
                size_t known_sizes = 0;
                for (size_t index = 1; index < listing.count; index++) {
                    size_t variant = (index - 1) % 7;
                    assert(listing.items[index].size == sizes[variant]);
                    assert(!!(listing.items[index].flags & NAV_ENTRY_SIZE_KNOWN) == (variant != 4));
                    assert(!!(listing.items[index].flags & NAV_ENTRY_SIZE_APPROXIMATE) == (variant >= 1 && variant <= 3));
                    assert(listing.items[index].flags & NAV_ENTRY_MODIFIED_KNOWN);
                    if (listing.items[index].flags & NAV_ENTRY_SIZE_KNOWN) known_sizes++;
                }
                printf("%zu known-size entries of %zu\n", known_sizes, listing.count - 1);
                assert(reports.calls > 10 && reports.increases > 10 && reports.entries == expected - 1);
                assert(reports.known == !strcmp(mode, "known"));
                if (reports.known) assert(reports.bytes == reports.total && reports.total > 1024 * 1024);
                else assert(reports.bytes > 50 * 1024 * 1024);
                printf("%s listing: %llu bytes, %zu unique entries, %zu progress calls\n", mode,
                       (unsigned long long)reports.bytes, reports.entries, reports.calls);
                if (!strcmp(mode, "large")) {
                    /* Reverse the fixture to exercise the former quadratic
                     * worst case, then sort by mixed exact/rounded sizes. */
                    for (size_t i = 1, j = listing.count - 1; i < j; i++, j--) {
                        NavEntry entry = listing.items[i]; listing.items[i] = listing.items[j]; listing.items[j] = entry;
                    }
                    NavPane *sorted = calloc(1, sizeof *sorted); assert(sorted);
                    sorted->listing = listing; sorted->selected = 100; sorted->directories_first = true;
                    char selected[NAV_PATH_MAX]; snprintf(selected, sizeof selected, "%s", nav_pane_selected(sorted)->resource_id);
                    clock_t start = clock(); nav_pane_sort(sorted, NAV_SORT_NAME);
                    double name_seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
                    assert(!strcmp(nav_pane_selected(sorted)->resource_id, selected));
                    assert(sorted->listing.items[0].flags & NAV_ENTRY_PARENT);
                    for (size_t i = 2; i < listing.count; i++) assert(strcmp(listing.items[i - 1].name, listing.items[i].name) < 0);
                    start = clock(); nav_pane_sort(sorted, NAV_SORT_SIZE);
                    double size_seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
                    for (size_t i = 2; i < listing.count; i++) assert(listing.items[i - 1].size <= listing.items[i].size);
                    printf("30k sort: reverse name %.3fs, mixed size %.3fs\n", name_seconds, size_seconds);
                    free(sorted);
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
    printf("NavEntry: %zu bytes; resource_id: %zu bytes\n", sizeof(NavEntry), sizeof(((NavEntry *)0)->resource_id));
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
