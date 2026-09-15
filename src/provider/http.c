#include "nav.h"
#include "nav_version.h"
#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HTTP_TAG_LIMIT (16u * 1024u)
#define HTTP_ROW_LIMIT (16u * 1024u)
#define HTTP_REDIRECT_LIMIT 5L

typedef struct
{
    NavProvider provider;
    CURL *easy;
    char name[NAV_REPO_NAME_MAX];
    char root[NAV_URL_MAX];
    char credential_name[NAV_CREDENTIAL_NAME_MAX];
    NavCredentialStore *credential_store;
    bool tls_verify;
    const NavConfig *config;
} HttpProvider;

void nav_http_provider_configure(NavProvider *provider, const NavConfig *config)
{
    if (provider && provider->scheme && !strcmp(provider->scheme, "http"))
        ((HttpProvider *)provider)->config = config;
}

typedef struct
{
    HttpProvider *http;
    NavListing *listing;
    CURL *transport;
    char current[NAV_URL_MAX];
    char tag[HTTP_TAG_LIMIT + 1];
    size_t length;
    char quote;
    char row[HTTP_ROW_LIMIT + 1];
    size_t row_length, row_bytes, entry_index;
    bool has_entry, metadata_text, table_row;
    unsigned comment_dashes;
    size_t *slots; /* entry index + 1; indexes survive listing reallocations */
    size_t slot_count, used;
    bool started, comment, memory_error, oversized, outside_root;
    const NavListOptions *options;
    NavListProgress progress;
    bool cancelled;
} HttpListingParser;

#ifdef NAV_HTTP_LISTING_TESTING
static size_t listing_allocations_remaining = SIZE_MAX;
void nav_http_test_listing_allocations(size_t remaining)
{
    listing_allocations_remaining = remaining;
}
#endif

static void *listing_allocate(void *old, size_t bytes)
{
#ifdef NAV_HTTP_LISTING_TESTING
    if (!listing_allocations_remaining) return NULL;
    if (listing_allocations_remaining != SIZE_MAX) listing_allocations_remaining--;
#endif
    return realloc(old, bytes);
}

typedef struct
{
    int kind;
    HttpProvider *provider;
    CURL *easy;
    CURLM *multi;
    unsigned char pending[CURL_MAX_WRITE_SIZE];
    size_t pending_offset, pending_length;
    unsigned char *target;
    size_t target_capacity, target_used;
    long response_status;
    long redirects;
    bool outside_root;
    bool paused, complete, failed;
    CURLcode result;
    char curl_error[CURL_ERROR_SIZE];
    char redirect_location[NAV_URL_MAX];
    NavResolvedCredential credential;
} HttpRead;

typedef struct
{
    int kind;
    HttpProvider *provider;
    CURL *easy;
    CURLM *multi;
    struct curl_slist *headers;
    const unsigned char *input;
    size_t input_length, input_offset;
    uint64_t bytes_sent;
    long response_status;
    bool paused, finishing, complete, failed;
    CURLcode result;
    char curl_error[CURL_ERROR_SIZE];
    NavResolvedCredential credential;
} HttpWrite;

typedef struct
{
    unsigned char *data;
    size_t capacity, length;
    bool exceeded;
    long status;
    uint64_t range_start, total;
    bool content_range_known;
} RangeResponse;

static bool curl_initialized;

static int ensure_curl(char *error, size_t error_size)
{
    if (curl_initialized) return 0;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        snprintf(error, error_size, "unable to initialize HTTP transport");
        return -1;
    }
    if (atexit(curl_global_cleanup)) {
        curl_global_cleanup();
        snprintf(error, error_size, "unable to register HTTP transport cleanup");
        return -1;
    }
    curl_initialized = true;
    return 0;
}

static int listing_append(NavListing *listing, const NavEntry *entry)
{
    if (listing->count == listing->capacity) {
        size_t capacity = listing->capacity ? listing->capacity * 2 : 32;
        if (capacity < listing->capacity || capacity > SIZE_MAX / sizeof(NavEntry))
            return -1;
        NavEntry *items = listing_allocate(listing->items, capacity * sizeof *items);
        if (!items) return -1;
        listing->items = items;
        listing->capacity = capacity;
    }
    listing->items[listing->count++] = *entry;
    return 0;
}

static bool url_is_within_root(const HttpProvider *http, const char *url)
{
    size_t length = strlen(http->root);
    char *decoded_root, *decoded_url;
    bool within;
    if (strncmp(url, http->root, length)) return false;
    decoded_root = curl_easy_unescape(http->easy, http->root, 0, NULL);
    decoded_url = curl_easy_unescape(http->easy, url, 0, NULL);
    if (!decoded_root || !decoded_url) {
        curl_free(decoded_root);
        curl_free(decoded_url);
        return false;
    }
    size_t decoded_length = strlen(decoded_url);
    within = !strncmp(decoded_url, decoded_root, strlen(decoded_root)) &&
             !strstr(decoded_url, "/../") && !strstr(decoded_url, "/./") &&
             !(decoded_length >= 3 &&
               !strcmp(decoded_url + decoded_length - 3, "/..")) &&
             !(decoded_length >= 2 &&
               !strcmp(decoded_url + decoded_length - 2, "/."));
    curl_free(decoded_root);
    curl_free(decoded_url);
    return within;
}

static int resolve_url(const char *base, const char *reference, char *output,
                       size_t capacity, char *error, size_t error_size)
{
    CURLU *url = curl_url();
    char *resolved = NULL, *scheme = NULL, *user = NULL, *password = NULL;
    CURLUcode code;
    if (!url) {
        snprintf(error, error_size, "unable to allocate URL parser");
        return -1;
    }
    code = curl_url_set(url, CURLUPART_URL, base, 0);
    if (!code && reference && reference[0])
        code = curl_url_set(url, CURLUPART_URL, reference, 0);
    if (!code) code = curl_url_get(url, CURLUPART_SCHEME, &scheme, 0);
    if (!code && strcasecmp(scheme, "http") && strcasecmp(scheme, "https"))
        code = CURLUE_UNSUPPORTED_SCHEME;
    if (!code && curl_url_get(url, CURLUPART_USER, &user, 0) == CURLUE_OK &&
        user && user[0]) code = CURLUE_USER_NOT_ALLOWED;
    if (!code && curl_url_get(url, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK &&
        password && password[0]) code = CURLUE_USER_NOT_ALLOWED;
    if (!code) code = curl_url_get(url, CURLUPART_URL, &resolved, 0);
    if (code || !resolved || strlen(resolved) >= capacity) {
        snprintf(error, error_size, "invalid HTTP repository URL: %s",
                 curl_url_strerror(code));
        curl_free(resolved); curl_free(scheme); curl_free(user); curl_free(password);
        curl_url_cleanup(url);
        return -1;
    }
    snprintf(output, capacity, "%s", resolved);
    curl_free(resolved); curl_free(scheme); curl_free(user); curl_free(password);
    curl_url_cleanup(url);
    return 0;
}

static int configure_request(HttpProvider *http, CURL *easy,
                             const char *resource_id,
                             NavResolvedCredential *credential,
                             char *error, size_t error_size)
{
    CURLcode code;
    memset(credential, 0, sizeof *credential);
    /* Empty explicitly bypasses environment proxies. NULL restores libcurl's
     * default discovery, including NO_PROXY; never mutate the environment. */
    code = curl_easy_setopt(easy, CURLOPT_PROXY,
                            http->config && http->config->proxy_mode == NAV_PROXY_NONE ? "" : NULL);
    if (code != CURLE_OK) {
        snprintf(error, error_size, "unable to configure HTTP proxy: %s", curl_easy_strerror(code));
        return -1;
    }
    curl_easy_setopt(easy, CURLOPT_URL, resource_id);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, HTTP_REDIRECT_LIMIT);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, NAV_APP_NAME "/" NAV_VERSION);
#if LIBCURL_VERSION_NUM >= 0x075500 /* 7.85.0 */
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR,
                     http->credential_name[0] ? "https" : "http,https");
#else
    /* Ubuntu 22.04's curl 7.81 uses bitmasks for the same restrictions. */
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS,
                     (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS,
                     http->credential_name[0] ? (long)CURLPROTO_HTTPS :
                     (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, http->tls_verify ? 1L : 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, http->tls_verify ? 2L : 0L);
    if (!http->credential_name[0]) return 0;
    if (!http->credential_store) {
        snprintf(error, error_size, "credential store is unavailable");
        return -1;
    }
    if (nav_credential_store_is_locked(http->credential_store)) {
        snprintf(error, error_size, "credential store is locked");
        return -1;
    }
    if (nav_credential_store_resolve(http->credential_store,
                                     http->credential_name, credential,
                                     error, error_size))
        return -1;
    if (credential->type == NAV_CREDENTIAL_BASIC) {
        code = curl_easy_setopt(easy, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        if (!code)
            code = curl_easy_setopt(easy, CURLOPT_USERNAME,
                                    credential->username ? credential->username : "");
        if (!code)
            code = curl_easy_setopt(easy, CURLOPT_PASSWORD,
                                    credential->secret ? credential->secret : "");
    } else if (credential->type == NAV_CREDENTIAL_BEARER) {
        code = curl_easy_setopt(easy, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
        if (!code)
            code = curl_easy_setopt(easy, CURLOPT_XOAUTH2_BEARER,
                                    credential->secret ? credential->secret : "");
    } else {
        snprintf(error, error_size,
                 "credential '%s' has an unsupported type",
                 http->credential_name);
        nav_resolved_credential_free(credential);
        return -1;
    }
    if (code != CURLE_OK) {
        snprintf(error, error_size, "unable to configure HTTP credentials: %s",
                 curl_easy_strerror(code));
        nav_resolved_credential_free(credential);
        return -1;
    }
    return 0;
}

static bool authentication_status_error(const HttpProvider *http, long status,
                                        char *error, size_t error_size)
{
    if (status == 401) {
        snprintf(error, error_size, "%s",
                 http->credential_name[0]
                     ? "repository authentication failed (401)"
                     : "repository requires authentication (401)");
        return true;
    }
    if (status == 403) {
        snprintf(error, error_size, "repository access was denied (403)");
        return true;
    }
    return false;
}

static int http_location(NavProvider *provider, const char *input,
                         NavLocation *output, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    char resolved[NAV_URL_MAX], relative[NAV_URL_MAX];
    const char *reference = input;
    if (!input || !input[0]) reference = http->root;
    else if (input[0] == '/') {
        if (snprintf(relative, sizeof relative, "%s%s", http->root, input + 1) >=
            (int)sizeof relative) {
            snprintf(error, error_size, "HTTP location is too long");
            return -1;
        }
        reference = relative;
    }
    if (strchr(reference, '?') || strchr(reference, '#') ||
        resolve_url(http->root, reference, resolved, sizeof resolved,
                    error, error_size))
        return -1;
    if (!url_is_within_root(http, resolved)) {
        snprintf(error, error_size, "location is outside repository root");
        return -1;
    }
    if (resolved[strlen(resolved) - 1] != '/') {
        size_t length = strlen(resolved);
        if (length + 1 >= sizeof resolved) {
            snprintf(error, error_size, "HTTP location is too long");
            return -1;
        }
        resolved[length] = '/';
        resolved[length + 1] = 0;
    }
    memset(output, 0, sizeof *output);
    output->provider = provider;
    snprintf(output->resource_id, sizeof output->resource_id, "%s", resolved);
    if (!strcmp(resolved, http->root))
        snprintf(output->display_path, sizeof output->display_path, "/");
    else
        snprintf(output->display_path, sizeof output->display_path, "/%s",
                 resolved + strlen(http->root));
    return 0;
}

static int http_child(NavProvider *provider, const NavLocation *parent,
                      const char *name, NavLocation *output, char *error,
                      size_t error_size)
{
    char escaped[NAV_URL_MAX], target[NAV_URL_MAX], resolved[NAV_URL_MAX];
    HttpProvider *http = provider->context;
    CURL *easy = http->easy;
    char *encoded;
    bool control = false;
    if (name) for (const unsigned char *ch = (const unsigned char *)name; *ch; ch++)
        if (iscntrl(*ch)) control = true;
    if (!parent || parent->provider != provider || !name || !name[0] ||
        !strcmp(name, ".") || !strcmp(name, "..") || strchr(name, '/') ||
        strchr(name, '\\') || control) {
        snprintf(error, error_size, "invalid HTTP child name");
        return -1;
    }
    encoded = curl_easy_escape(easy, name, 0);
    if (!encoded) {
        snprintf(error, error_size, "unable to encode HTTP child name");
        return -1;
    }
    snprintf(escaped, sizeof escaped, "%s", encoded);
    curl_free(encoded);
    if (snprintf(target, sizeof target, "%s%s", parent->resource_id, escaped) >=
        (int)sizeof target) {
        snprintf(error, error_size, "HTTP child URL is too long");
        return -1;
    }
    if (resolve_url(parent->resource_id, target, resolved, sizeof resolved,
                    error, error_size) || !url_is_within_root(http, resolved)) {
        if (!error[0]) snprintf(error, error_size,
                                "HTTP child is outside repository root");
        return -1;
    }
    memset(output, 0, sizeof *output);
    output->provider = provider;
    snprintf(output->resource_id, sizeof output->resource_id, "%s", resolved);
    snprintf(output->display_path, sizeof output->display_path, "/%s",
             resolved + strlen(http->root));
    return 0;
}

static int http_parent(NavProvider *provider, const NavLocation *location,
                       NavLocation *output, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    char parent[NAV_URL_MAX], *slash;
    if (!location || location->provider != provider ||
        !url_is_within_root(http, location->resource_id)) {
        snprintf(error, error_size, "invalid HTTP location");
        return -1;
    }
    if (!strcmp(location->resource_id, http->root))
        return http_location(provider, http->root, output, error, error_size);
    if (strlen(location->resource_id) >= sizeof parent) {
        snprintf(error, error_size, "HTTP location is too long");
        return -1;
    }
    memcpy(parent, location->resource_id, strlen(location->resource_id) + 1);
    if (parent[strlen(parent) - 1] == '/') parent[strlen(parent) - 1] = 0;
    slash = strrchr(parent, '/');
    if (!slash || (size_t)(slash + 1 - parent) < strlen(http->root))
        snprintf(parent, sizeof parent, "%s", http->root);
    else
        slash[1] = 0;
    return http_location(provider, parent, output, error, error_size);
}

static size_t listing_hash(const char *url)
{
    size_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        hash = (hash ^ *p) * 16777619u;
    return hash;
}

static size_t listing_slot(const HttpListingParser *parser, const char *url)
{
    size_t slot = listing_hash(url) & (parser->slot_count - 1);
    while (parser->slots[slot] &&
           strcmp(parser->listing->items[parser->slots[slot] - 1].resource_id, url))
        slot = (slot + 1) & (parser->slot_count - 1);
    return slot;
}

static int listing_grow_index(HttpListingParser *parser)
{
    if (parser->slot_count && parser->used < parser->slot_count / 2) return 0;
    size_t count = parser->slot_count ? parser->slot_count * 2 : 64;
    if (count < parser->slot_count || count > SIZE_MAX / sizeof(size_t)) return -1;
    size_t *slots = listing_allocate(NULL, count * sizeof *slots);
    if (!slots) return -1;
    memset(slots, 0, count * sizeof *slots);
    size_t *old = parser->slots, old_count = parser->slot_count;
    parser->slots = slots;
    parser->slot_count = count;
    for (size_t index = 0; index < old_count; index++) {
        if (!old[index]) continue;
        const char *url = parser->listing->items[old[index] - 1].resource_id;
        slots[listing_slot(parser, url)] = old[index];
    }
    free(old);
    return 0;
}

static int add_link(HttpProvider *http, const char *current, const char *href,
                    HttpListingParser *parser)
{
    NavListing *listing = parser->listing;
    char resolved[NAV_URL_MAX], path[NAV_URL_MAX], name[NAV_NAME_MAX];
    CURLU *url;
    char *url_path = NULL, *decoded = NULL;
    size_t length;
    bool directory;
    NavEntry entry = {0};
    if (!href[0] || href[0] == '#' || href[0] == '?' || strchr(href, '?') ||
        strchr(href, '#') || !strcmp(href, "../") || !strcmp(href, ".."))
        return 0;
    if (resolve_url(current, href, resolved, sizeof resolved, path, sizeof path) ||
        !url_is_within_root(http, resolved) || !strcmp(resolved, current))
        return 0;
    directory = href[strlen(href) - 1] == '/';
    url = curl_url();
    if (!url) return -1;
    CURLUcode code = curl_url_set(url, CURLUPART_URL, resolved, 0);
    if (!code) code = curl_url_get(url, CURLUPART_PATH, &url_path, 0);
    if (code) {
        curl_url_cleanup(url);
        return code == CURLUE_OUT_OF_MEMORY ? -1 : 0;
    }
    snprintf(path, sizeof path, "%s", url_path);
    curl_free(url_path);
    curl_url_cleanup(url);
    length = strlen(path);
    while (length > 1 && path[length - 1] == '/') path[--length] = 0;
    const char *leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    int decoded_length = 0;
    decoded = curl_easy_unescape(http->easy, leaf, 0, &decoded_length);
    if (!decoded) return -1;
    bool invalid_name = false;
    for (int index = 0; index < decoded_length; index++)
        if (iscntrl((unsigned char)decoded[index])) invalid_name = true;
    if (invalid_name || !decoded[0] || !strcmp(decoded, ".") ||
        !strcmp(decoded, "..") || strchr(decoded, '/') || strchr(decoded, '\\') ||
        strlen(decoded) >= sizeof name) {
        curl_free(decoded);
        return 0;
    }
    snprintf(name, sizeof name, "%s", decoded);
    curl_free(decoded);
    snprintf(entry.name, sizeof entry.name, "%s", name);
    snprintf(entry.resource_id, sizeof entry.resource_id, "%s", resolved);
    if (directory) entry.flags |= NAV_ENTRY_DIR;
    if (listing_grow_index(parser)) return -1;
    size_t slot = listing_slot(parser, resolved);
    if (parser->slots[slot]) {
        parser->entry_index = parser->slots[slot] - 1;
        parser->has_entry = true;
        return 0;
    }
    if (listing_append(listing, &entry)) return -1;
    parser->slots[slot] = listing->count;
    parser->used++;
    parser->entry_index = listing->count - 1;
    parser->has_entry = true;
    return 0;
}

static int listing_start(HttpListingParser *parser, const char *current)
{
    NavEntry parent = {0};
    if (strlen(current) >= sizeof parser->current) return -1;
    snprintf(parser->current, sizeof parser->current, "%s", current);
    snprintf(parent.name, sizeof parent.name, "..");
    snprintf(parent.resource_id, sizeof parent.resource_id, "%s", current);
    parent.flags = NAV_ENTRY_DIR | NAV_ENTRY_PARENT;
    if (listing_append(parser->listing, &parent)) return -1;
    parser->started = true;
    return 0;
}

static bool listing_size(const char *text, uint64_t *size, bool *approximate)
{
    uint64_t value = 0, multiplier = 1;
    long double fraction = 0, place = 0.1L;
    bool fractional = false;
    if (!text) return false;
    while (isspace((unsigned char)*text)) text++;
    if (!isdigit((unsigned char)*text)) return false;
    while (isdigit((unsigned char)*text)) {
        unsigned digit = (unsigned)(*text++ - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (*text == '.') {
        text++; fractional = true;
        if (!isdigit((unsigned char)*text)) return false;
        unsigned digits = 0;
        while (isdigit((unsigned char)*text)) {
            if (++digits > 18) return false;
            fraction += (*text++ - '0') * place; place /= 10;
        }
    }
    while (isspace((unsigned char)*text)) text++;
    char unit[4]; size_t length = 0;
    while (*text && !isspace((unsigned char)*text)) {
        if (length == sizeof unit - 1) return false;
        unit[length++] = (char)tolower((unsigned char)*text++);
    }
    unit[length] = 0;
    while (isspace((unsigned char)*text)) text++;
    if (*text) return false;
    unsigned power = 0;
    if (length && strcmp(unit, "b")) {
        const char *units = "kmgt", *prefix = strchr(units, unit[0]);
        if (!prefix || (strcmp(unit + 1, "") && strcmp(unit + 1, "b") && strcmp(unit + 1, "ib"))) return false;
        power = (unsigned)(prefix - units) + 1;
        for (unsigned i = 0; i < power; i++) multiplier *= 1024;
    }
    if (fractional && !length) return false;
    if (value > UINT64_MAX / multiplier) return false;
    value *= multiplier;
    /* Only the fractional component uses floating point; integer byte counts,
     * including UINT64_MAX, never pass through a lossy conversion. */
    uint64_t extra = (uint64_t)(fraction * multiplier);
    if (value > UINT64_MAX - extra) return false;
    *size = value + extra;
    *approximate = power != 0 || fractional;
    return true;
}

/* Consume one size field, not the description following it. */
static bool listing_size_field(const char *text, bool cell, uint64_t *size, bool *approximate)
{
    char field[64];
    size_t length = 0;
    while (isspace((unsigned char)*text) && (!cell || *text != '\t')) text++;
    while (*text && !isspace((unsigned char)*text)) {
        if (length + 1 >= sizeof field) return false;
        field[length++] = *text++;
    }
    field[length] = 0;
    /* A separated unit belongs to the size only if it is a valid unit.
     * Tabs are retained HTML cell boundaries: never borrow a description cell. */
    while (isspace((unsigned char)*text) && (!cell || *text != '\t')) text++;
    char unit[4]; size_t n = 0;
    while (text[n] && !isspace((unsigned char)text[n]) && n < sizeof unit - 1) {
        unit[n] = text[n]; n++;
    }
    unit[n] = 0;
    uint64_t ignored; bool rounded;
    char probe[16];
    snprintf(probe, sizeof probe, "1 %s", unit);
    if (strspn(field, "0123456789.") == length && n &&
        (!text[n] || isspace((unsigned char)text[n])) &&
        listing_size(probe, &ignored, &rounded) && length + n + 2 <= sizeof field) {
        field[length++] = ' ';
        memcpy(field + length, unit, n + 1);
    }
    return listing_size(field, size, approximate);
}

/* Decode only whitespace entities needed to delimit metadata. Other entities
 * remain text; they must not accidentally turn a description into a size. */
static void listing_metadata_spaces(char *row)
{
    char *out = row;
    while (*row) {
        size_t length = 0;
        if (!strncmp(row, "&nbsp;", 6)) length = 6;
        else if (!strncmp(row, "&#160;", 6)) length = 6;
        else if (!strncmp(row, "&#32;", 5)) length = 5;
        else if (!strncasecmp(row, "&#xa0;", 6)) length = 6;
        else if (!strncasecmp(row, "&#x20;", 6)) length = 6;
        if (length) { *out++ = ' '; row += length; }
        else *out++ = *row++;
    }
    *out = 0;
}

static bool listing_date(const char *date, const char *clock, time_t *modified)
{
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int day, year, hour, minute, consumed = 0, month = -1;
    char name[4];
    if (strlen(date) == 10) {
        int number;
        if (sscanf(date, "%4d-%2d-%2d%n", &year, &number, &day, &consumed) != 3 || consumed != 10)
            return false;
        month = number - 1;
    } else {
        if (strlen(date) != 11 || sscanf(date, "%2d-%3[A-Za-z]-%4d%n", &day, name, &year, &consumed) != 3 || consumed != 11)
            return false;
        for (int index = 0; index < 12; index++)
            if (!strcasecmp(name, months[index])) month = index;
    }
    if (strlen(clock) != 5 || sscanf(clock, "%2d:%2d%n", &hour, &minute, &consumed) != 2 ||
        consumed != 5 || year < 1900 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || month < 0 || month > 11) return false;
    /* nginx HTML has no timezone. Interpret its wall clock in the client's
     * local timezone, matching autoindex_localtime on when zones agree. */
    struct tm value = {.tm_year = year - 1900, .tm_mon = month, .tm_mday = day,
                       .tm_hour = hour, .tm_min = minute, .tm_isdst = -1};
    time_t stamp = mktime(&value);
    struct tm check;
    if (!nav_platform_localtime(&stamp, &check)) return false;
    if (check.tm_year != year - 1900 || check.tm_mon != month || check.tm_mday != day ||
        check.tm_hour != hour || check.tm_min != minute) return false;
    *modified = stamp;
    return true;
}

static void listing_finish_row(HttpListingParser *parser)
{
    if (parser->has_entry && parser->metadata_text) {
        char *parts[2] = {0}, *cursor = parser->row;
        size_t count = 0;
        parser->row[parser->row_length] = 0;
        listing_metadata_spaces(parser->row);
        /* Preserve size-field boundaries, including separated unit tokens. */
        char original[HTTP_ROW_LIMIT + 1];
        snprintf(original, sizeof original, "%s", parser->row);
        while (*cursor && count < 2) {
            while (*cursor && isspace((unsigned char)*cursor)) cursor++;
            if (!*cursor) break;
            parts[count++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
            if (*cursor) *cursor++ = 0;
        }
        NavEntry *entry = &parser->listing->items[parser->entry_index];
        uint64_t size;
        bool approximate;
        time_t modified;
        if (count >= 2 && !(entry->flags & NAV_ENTRY_MODIFIED_KNOWN) &&
            listing_date(parts[0], parts[1], &modified)) {
            entry->modified = modified;
            entry->flags |= NAV_ENTRY_MODIFIED_KNOWN;
        }
        const char *size_text = count == 2 && (strlen(parts[0]) == 11 || strlen(parts[0]) == 10) && strlen(parts[1]) == 5 ? cursor : original;
        /* In a table, the cell after the date is the size, even if empty.
         * Do not skip empty size cells and consume numeric descriptions. */
        const char *first = original;
        while (isspace((unsigned char)*first)) first++;
        const char *clock_field = first;
        while (*clock_field && !isspace((unsigned char)*clock_field)) clock_field++;
        while (isspace((unsigned char)*clock_field)) clock_field++;
        const char *boundary = strchr(clock_field, '\t');
        if (parser->table_row && boundary && size_text == cursor) size_text = boundary + 1;
        else if (parser->table_row && size_text == original) {
            const char *leading = original;
            while (*leading == ' ') leading++;
            if (*leading == '\t') size_text = leading + 1;
        }
        if (!(entry->flags & (NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_DIR)) && listing_size_field(size_text, parser->table_row, &size, &approximate)) {
            entry->size = size;
            entry->flags |= NAV_ENTRY_SIZE_KNOWN;
            if (approximate) entry->flags |= NAV_ENTRY_SIZE_APPROXIMATE;
        }
    }
    parser->has_entry = parser->metadata_text = false;
    parser->row_length = parser->row_bytes = 0;
}

static bool listing_tag_is(const HttpListingParser *parser, const char *name)
{
    size_t length = strlen(name);
    return parser->length >= length + 2 &&
           !strncasecmp(parser->tag + 1, name, length) &&
           (isspace((unsigned char)parser->tag[length + 1]) ||
            parser->tag[length + 1] == '>' || parser->tag[length + 1] == '/');
}

/* Parse one complete tag. The transport and standalone helper share this code. */
static int listing_tag(HttpListingParser *parser)
{
    char *attribute = parser->tag + 2;
    char *end = parser->tag + parser->length - 1;
    if (listing_tag_is(parser, "tr") || listing_tag_is(parser, "/tr") ||
        listing_tag_is(parser, "br") || listing_tag_is(parser, "/pre")) {
        listing_finish_row(parser);
        if (listing_tag_is(parser, "tr")) parser->table_row = true;
        if (listing_tag_is(parser, "/tr")) parser->table_row = false;
        return 0;
    }
    if (listing_tag_is(parser, "/a")) {
        parser->metadata_text = parser->has_entry;
        return 0;
    }
    if (!listing_tag_is(parser, "a")) {
        if (parser->metadata_text && parser->row_length < HTTP_ROW_LIMIT)
            parser->row[parser->row_length++] = listing_tag_is(parser, "/td") ||
                listing_tag_is(parser, "/th") ? '\t' : ' ';
        return 0;
    }
    listing_finish_row(parser);
    parser->row_bytes = parser->length;
    while (attribute < end) {
        char *name_start, *value_start;
        size_t name_length, value_length;
        char quote = 0;
        while (attribute < end && isspace((unsigned char)*attribute)) attribute++;
        name_start = attribute;
        while (attribute < end &&
               (isalnum((unsigned char)*attribute) || *attribute == '-' ||
                *attribute == '_')) attribute++;
        name_length = (size_t)(attribute - name_start);
        while (attribute < end && isspace((unsigned char)*attribute)) attribute++;
        if (attribute >= end || *attribute != '=') {
            if (attribute == name_start) attribute++;
            continue;
        }
        attribute++;
        while (attribute < end && isspace((unsigned char)*attribute)) attribute++;
        if (attribute < end && (*attribute == '"' || *attribute == '\''))
            quote = *attribute++;
        value_start = attribute;
        while (attribute < end &&
               (quote ? *attribute != quote : !isspace((unsigned char)*attribute)))
            attribute++;
        value_length = (size_t)(attribute - value_start);
        if (quote && attribute < end) attribute++;
        if (name_length == 4 && !strncasecmp(name_start, "href", 4)) {
            /* Reject oversized URLs rather than accidentally linking a prefix. */
            if (value_length >= NAV_URL_MAX) return 0;
            /* The completed tag is ours; terminate href in place. */
            value_start[value_length] = 0;
            return add_link(parser->http, parser->current, value_start, parser);
        }
    }
    return 0;
}

static size_t receive_listing_data(char *data, size_t size, size_t count,
                                    void *userdata)
{
    HttpListingParser *parser = userdata;
    if (size && count > SIZE_MAX / size) {
        parser->oversized = true;
        return 0;
    }
    size_t amount = size * count;
    if (parser->transport) {
        long status = 0;
        char *effective = NULL;
        curl_easy_getinfo(parser->transport, CURLINFO_RESPONSE_CODE, &status);
        /* Redirect/error bodies must not become directory entries. */
        if (status < 200 || status >= 300) return amount;
        if (!parser->started) {
            curl_easy_getinfo(parser->transport, CURLINFO_EFFECTIVE_URL, &effective);
            if (!effective || !url_is_within_root(parser->http, effective)) {
                parser->outside_root = true;
                return 0;
            }
            if (listing_start(parser, effective)) {
                parser->memory_error = true;
                return 0;
            }
        }
    }
    for (size_t index = 0; index < amount; index++) {
        char ch = data[index];
        if (parser->has_entry && ++parser->row_bytes > HTTP_ROW_LIMIT) {
            parser->oversized = true;
            return 0;
        }
        if (parser->comment) {
            if (ch == '>' && parser->comment_dashes == 2) {
                parser->comment = false;
                parser->comment_dashes = 0;
            } else if (ch == '-') {
                if (parser->comment_dashes < 2) parser->comment_dashes++;
            } else parser->comment_dashes = 0;
            continue;
        }
        if (!parser->length) {
            if (ch != '<') {
                if (parser->metadata_text) {
                    if ((ch == '\n' || ch == '\r') && !parser->table_row) listing_finish_row(parser);
                    else if (ch == '\n' || ch == '\r') parser->row[parser->row_length++] = ' ';
                    else parser->row[parser->row_length++] = parser->table_row && ch == '\t' ? ' ' : ch;
                }
                continue;
            }
        } else if (ch == '<' && !parser->quote) {
            /* Recover from an unfinished, unquoted tag before a fresh tag. */
            parser->length = 0;
        }
        if (parser->length == HTTP_TAG_LIMIT) {
            parser->oversized = true;
            return 0;
        }
        parser->tag[parser->length++] = ch;
        if (parser->length == 4 && !memcmp(parser->tag, "<!--", 4)) {
            if (parser->metadata_text)
                parser->row[parser->row_length++] = ' ';
            parser->comment = true;
            parser->length = 0;
            continue;
        }
        if (ch == parser->quote) parser->quote = 0;
        else if (!parser->quote && (ch == '"' || ch == '\'')) parser->quote = ch;
        if (ch == '>' && !parser->quote) {
            parser->tag[parser->length] = 0;
            if (listing_tag(parser)) {
                parser->memory_error = true;
                return 0;
            }
            parser->length = 0;
        }
    }
    parser->progress.bytes_received += amount;
    parser->progress.entries = parser->used;
    if (parser->options && parser->options->progress)
        parser->options->progress(&parser->progress, parser->options->userdata);
    return amount;
}

static int listing_progress(void *data, curl_off_t total, curl_off_t received,
                            curl_off_t upload_total, curl_off_t uploaded)
{
    HttpListingParser *parser = data;
    (void)received; (void)upload_total; (void)uploaded;
    curl_off_t length = -1;
    curl_easy_getinfo(parser->transport, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    parser->progress.total_known = total > 0 || length >= 0;
    parser->progress.total_bytes = total > 0 ? (uint64_t)total : length >= 0 ? (uint64_t)length : 0;
    if (parser->options && parser->options->cancel &&
        parser->options->cancel(parser->options->userdata)) {
        parser->cancelled = true;
        return 1;
    }
    if (parser->options && parser->options->progress)
        parser->options->progress(&parser->progress, parser->options->userdata);
    return 0;
}

static void listing_error(const HttpListingParser *parser, char *error,
                          size_t error_size)
{
    snprintf(error, error_size, "%s", parser->memory_error ?
             "out of memory parsing HTTP directory listing" :
             parser->outside_root ? "HTTP redirect left repository root" :
             "HTTP directory listing row/tag exceeds 16 KiB parser limit");
}

static int parse_listing_chunks(NavProvider *provider, const char *current,
                                const char *html, size_t length, size_t chunk,
                                NavListing *listing, char *error, size_t error_size)
{
    if (!provider || !provider->context || !current || !html || !listing || !chunk) {
        snprintf(error, error_size, "invalid HTTP directory listing");
        return -1;
    }
    HttpListingParser parser = {.http = provider->context, .listing = listing};
    nav_listing_free(listing);
    if (listing_start(&parser, current)) parser.memory_error = true;
    for (size_t offset = 0; offset < length && !parser.memory_error && !parser.oversized;) {
        size_t amount = length - offset < chunk ? length - offset : chunk;
        if (receive_listing_data((char *)html + offset, 1, amount, &parser) != amount)
            break;
        offset += amount;
    }
    listing_finish_row(&parser);
    free(parser.slots);
    if (parser.memory_error || parser.oversized) {
        listing_error(&parser, error, error_size);
        nav_listing_free(listing);
        return -1;
    }
    /* An incomplete final tag is ignored; only complete tags emit entries. */
    return 0;
}

#ifdef NAV_HTTP_LISTING_TESTING
int nav_http_test_parse_chunks(NavProvider *provider, const char *current,
                              const char *html, size_t length, size_t chunk,
                              NavListing *listing, char *error, size_t error_size)
{
    return parse_listing_chunks(provider, current, html, length, chunk,
                                listing, error, error_size);
}
#endif

int nav_http_parse_directory_html(NavProvider *provider, const char *current,
                                  const char *html, NavListing *listing,
                                  char *error, size_t error_size)
{
    return parse_listing_chunks(provider, current, html, html ? strlen(html) : 0,
                                SIZE_MAX, listing, error, error_size);
}

static size_t read_header(char *data, size_t size, size_t count, void *userdata)
{
    HttpRead *read = userdata;
    size_t amount;
    if (size && count > SIZE_MAX / size) return 0;
    amount = size * count;
    if (amount >= 5 && !strncasecmp(data, "HTTP/", 5)) {
        long status = 0;
        read->redirect_location[0] = 0;
        if (sscanf(data, "HTTP/%*s %ld", &status) == 1)
            read->response_status = status;
    } else if (read->response_status >= 300 && read->response_status < 400 &&
               amount >= 9 && !strncasecmp(data, "Location:", 9)) {
        const char *start = data + 9, *end = data + amount;
        while (start < end && isspace((unsigned char)*start)) start++;
        while (end > start && isspace((unsigned char)end[-1])) end--;
        size_t length = (size_t)(end - start);
        if (length >= sizeof read->redirect_location ||
            memchr(start, 0, length)) return 0;
        memcpy(read->redirect_location, start, length);
        read->redirect_location[length] = 0;
    }
    return amount;
}

static size_t stream_data(char *data, size_t size, size_t count, void *userdata)
{
    HttpRead *read = userdata;
    size_t amount, available, copy;
    if (size && count > SIZE_MAX / size) return 0;
    amount = size * count;
    if (read->response_status < 200 || read->response_status >= 300)
        return amount;
    /* Check before copying into either the caller's buffer or pending data. */
    char *effective = NULL;
    if (curl_easy_getinfo(read->easy, CURLINFO_EFFECTIVE_URL, &effective) !=
            CURLE_OK || !effective ||
        !url_is_within_root(read->provider, effective)) {
        read->outside_root = true;
        return 0;
    }
    if (read->pending_offset < read->pending_length) {
        read->paused = true;
        return CURL_WRITEFUNC_PAUSE;
    }
    available = read->target_capacity - read->target_used;
    if (amount <= available) {
        memcpy(read->target + read->target_used, data, amount);
        read->target_used += amount;
        return amount;
    }
    if (amount > sizeof read->pending) return 0;
    memcpy(read->pending, data, amount);
    read->pending_offset = 0;
    read->pending_length = amount;
    copy = available < amount ? available : amount;
    if (copy) {
        memcpy(read->target + read->target_used, read->pending, copy);
        read->target_used += copy;
        read->pending_offset += copy;
    }
    return amount;
}

static int http_open_read(NavProvider *provider, const char *resource_id,
                          void **handle, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    HttpRead *read;
    if (!resource_id || !url_is_within_root(http, resource_id)) {
        snprintf(error, error_size, "HTTP resource is outside repository root");
        return -1;
    }
    read = calloc(1, sizeof *read);
    if (!read) {
        snprintf(error, error_size, "out of memory opening HTTP resource");
        return -1;
    }
    read->kind = 1;
    read->provider = http;
    read->easy = curl_easy_init();
    read->multi = curl_multi_init();
    if (!read->easy || !read->multi) {
        if (read->easy) curl_easy_cleanup(read->easy);
        if (read->multi) curl_multi_cleanup(read->multi);
        free(read);
        snprintf(error, error_size, "unable to initialize HTTP read");
        return -1;
    }
    if (configure_request(http, read->easy, resource_id, &read->credential,
                          error, error_size)) {
        nav_resolved_credential_free(&read->credential);
        curl_multi_cleanup(read->multi);
        curl_easy_cleanup(read->easy);
        free(read);
        return -1;
    }
    /* Only read_redirect may advance to another, validated target. */
    curl_easy_setopt(read->easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(read->easy, CURLOPT_WRITEFUNCTION, stream_data);
    curl_easy_setopt(read->easy, CURLOPT_WRITEDATA, read);
    curl_easy_setopt(read->easy, CURLOPT_HEADERFUNCTION, read_header);
    curl_easy_setopt(read->easy, CURLOPT_HEADERDATA, read);
    curl_easy_setopt(read->easy, CURLOPT_ERRORBUFFER, read->curl_error);
    if (curl_multi_add_handle(read->multi, read->easy) != CURLM_OK) {
        nav_resolved_credential_free(&read->credential);
        curl_multi_cleanup(read->multi);
        curl_easy_cleanup(read->easy);
        free(read);
        snprintf(error, error_size, "unable to start HTTP read");
        return -1;
    }
    *handle = read;
    return 0;
}

static void read_completion(HttpRead *read)
{
    int messages = 0;
    CURLMsg *message;
    while ((message = curl_multi_info_read(read->multi, &messages)))
        if (message->msg == CURLMSG_DONE) {
            read->complete = true;
            read->result = message->data.result;
        }
}

static int read_redirect(HttpRead *read, char *error, size_t error_size)
{
    char *redirect = NULL, *effective = NULL;
    char target[NAV_URL_MAX];
    CURLcode code;
    CURLMcode multi_code;
    if (!read->complete || read->result != CURLE_OK) return 0;
    code = curl_easy_getinfo(read->easy, CURLINFO_REDIRECT_URL, &redirect);
    if (code != CURLE_OK) goto curl_error;
    if (!redirect) return 0;
    if (read->redirects >= HTTP_REDIRECT_LIMIT) {
        code = CURLE_TOO_MANY_REDIRECTS;
        goto curl_error;
    }
    code = curl_easy_getinfo(read->easy, CURLINFO_EFFECTIVE_URL, &effective);
    if (code != CURLE_OK) goto curl_error;
    /* REDIRECT_URL identifies redirects libcurl would follow, but can contain
       configured credentials. Resolve the original Location instead, retaining
       the existing parser's rejection of credentials supplied in URLs. */
    if (!effective || !read->redirect_location[0]) {
        snprintf(error, error_size, "invalid HTTP redirect target");
        return -1;
    }
    if (resolve_url(effective, read->redirect_location, target, sizeof target,
                    error, error_size))
        return -1;
    if (!url_is_within_root(read->provider, target)) {
        snprintf(error, error_size, "HTTP redirect left repository root");
        return -1;
    }
    multi_code = curl_multi_remove_handle(read->multi, read->easy);
    if (multi_code != CURLM_OK) goto multi_error;
    code = curl_easy_setopt(read->easy, CURLOPT_URL, target);
    if (code != CURLE_OK) goto curl_error;
    read->redirects++;
    read->response_status = 0;
    read->complete = false;
    read->curl_error[0] = 0;
    /* Reuse the configured handle and its owned credential for in-root hops. */
    multi_code = curl_multi_add_handle(read->multi, read->easy);
    if (multi_code != CURLM_OK) goto multi_error;
    return 0;
curl_error:
    snprintf(error, error_size, "HTTP read failed: %s", curl_easy_strerror(code));
    return -1;
multi_error:
    snprintf(error, error_size, "HTTP read failed: %s",
             curl_multi_strerror(multi_code));
    return -1;
}

static int http_read_cancellable(NavProvider *provider, void *handle, void *buffer,
                     size_t capacity, size_t *got, NavCancelFn cancel, void *data, char *error,
                     size_t error_size)
{
    HttpRead *read = handle;
    int running = 0;
    CURLMcode multi_code;
    (void)provider;
    if (!read || !buffer || !got) return -1;
    *got = 0;
    if (read->failed) {
        snprintf(error, error_size, "HTTP read has failed");
        return -1;
    }
    if (!capacity) return 0;
    read->target = buffer;
    read->target_capacity = capacity;
    read->target_used = 0;
    if (read->pending_offset < read->pending_length) {
        size_t available = read->pending_length - read->pending_offset;
        size_t amount = available < capacity ? available : capacity;
        memcpy(read->target, read->pending + read->pending_offset, amount);
        read->pending_offset += amount;
        read->target_used += amount;
        if (read->pending_offset == read->pending_length)
            read->pending_offset = read->pending_length = 0;
    }
    if (read->target_used == capacity) {
        *got = read->target_used;
        return 0;
    }
    if (read->paused && read->pending_length == 0) {
        read->paused = false;
        if (curl_easy_pause(read->easy, CURLPAUSE_CONT) != CURLE_OK) {
            snprintf(error, error_size, "unable to resume HTTP read");
            read->failed = true;
            return -1;
        }
    }
    while (read->target_used < capacity && !read->complete) {
        if (cancel && cancel(data)) {
            snprintf(error, error_size, "Download cancelled"); read->failed = true; return -1;
        }
        multi_code = curl_multi_perform(read->multi, &running);
        if (multi_code != CURLM_OK) {
            snprintf(error, error_size, "HTTP read failed: %s",
                     curl_multi_strerror(multi_code));
            read->failed = true;
            return -1;
        }
        read_completion(read);
        if (read_redirect(read, error, error_size)) {
            read->failed = true;
            return -1;
        }
        if (read->target_used == capacity || read->paused || read->complete) break;
        if (running) {
            multi_code = curl_multi_poll(read->multi, NULL, 0, cancel ? 100 : 1000, NULL);
            if (multi_code != CURLM_OK) {
                snprintf(error, error_size, "HTTP read failed: %s",
                         curl_multi_strerror(multi_code));
                read->failed = true;
                return -1;
            }
        }
    }
    if (read->outside_root) {
        snprintf(error, error_size, "HTTP redirect left repository root");
        read->failed = true;
        return -1;
    }
    if (read->complete && read->result != CURLE_OK) {
        snprintf(error, error_size, "HTTP read failed: %s",
                 read->curl_error[0] ? read->curl_error :
                 curl_easy_strerror(read->result));
        read->failed = true;
        return -1;
    }
    if (read->complete &&
        (read->response_status < 200 || read->response_status >= 300)) {
        if (!authentication_status_error(read->provider, read->response_status,
                                         error, error_size))
            snprintf(error, error_size, "HTTP server returned status %ld",
                     read->response_status);
        read->failed = true;
        return -1;
    }
    if (read->complete) {
        char *effective = NULL;
        curl_easy_getinfo(read->easy, CURLINFO_EFFECTIVE_URL, &effective);
        if (!effective || !url_is_within_root(read->provider, effective)) {
            snprintf(error, error_size, "HTTP redirect left repository root");
            read->failed = true;
            return -1;
        }
    }
    *got = read->target_used;
    read->target = NULL;
    read->target_capacity = read->target_used = 0;
    return 0;
}

static int http_read(NavProvider *provider, void *handle, void *buffer,
                     size_t capacity, size_t *got, char *error, size_t error_size)
{
    return http_read_cancellable(provider, handle, buffer, capacity, got, NULL,
                                 NULL, error, error_size);
}

static int http_close_read(NavProvider *provider, void *handle, char *error,
                           size_t error_size)
{
    HttpRead *read = handle;
    (void)provider; (void)error; (void)error_size;
    if (!read) return 0;
    curl_multi_remove_handle(read->multi, read->easy);
    curl_multi_cleanup(read->multi);
    curl_easy_cleanup(read->easy);
    nav_resolved_credential_free(&read->credential);
    free(read);
    return 0;
}

static size_t upload_read(char *data, size_t size, size_t count, void *userdata)
{
    HttpWrite *write = userdata;
    size_t capacity, available, amount;
    if (size && count > SIZE_MAX / size) return CURL_READFUNC_ABORT;
    capacity = size * count;
    available = write->input_length - write->input_offset;
    if (!available) {
        if (write->finishing) return 0;
        write->paused = true;
        return CURL_READFUNC_PAUSE;
    }
    amount = available < capacity ? available : capacity;
    memcpy(data, write->input + write->input_offset, amount);
    write->input_offset += amount;
    write->bytes_sent += (uint64_t)amount;
    return amount;
}

static size_t upload_response(char *data, size_t size, size_t count,
                              void *userdata)
{
    size_t amount;
    (void)data; (void)userdata;
    if (size && count > SIZE_MAX / size) return 0;
    amount = size * count;
    return amount;
}

static size_t upload_header(char *data, size_t size, size_t count,
                            void *userdata)
{
    HttpWrite *write = userdata;
    size_t amount;
    if (size && count > SIZE_MAX / size) return 0;
    amount = size * count;
    if (amount >= 5 && !strncasecmp(data, "HTTP/", 5)) {
        long status = 0;
        if (sscanf(data, "HTTP/%*s %ld", &status) == 1)
            write->response_status = status;
    }
    return amount;
}

static void upload_completion(HttpWrite *write)
{
    int messages = 0;
    CURLMsg *message;
    while ((message = curl_multi_info_read(write->multi, &messages)))
        if (message->msg == CURLMSG_DONE) {
            write->complete = true;
            write->result = message->data.result;
        }
}

static int upload_drive(HttpWrite *write, bool until_complete, char *error,
                        size_t error_size)
{
    int running = 0;
    while (!write->complete &&
           (until_complete || write->input_offset < write->input_length)) {
        CURLMcode code = curl_multi_perform(write->multi, &running);
        if (code != CURLM_OK) {
            snprintf(error, error_size, "HTTP upload failed: %s",
                     curl_multi_strerror(code));
            write->failed = true;
            return -1;
        }
        upload_completion(write);
        if (write->complete ||
            (!until_complete && write->input_offset == write->input_length))
            break;
        if (running) {
            code = curl_multi_poll(write->multi, NULL, 0, 1000, NULL);
            if (code != CURLM_OK) {
                snprintf(error, error_size, "HTTP upload failed: %s",
                         curl_multi_strerror(code));
                write->failed = true;
                return -1;
            }
        }
    }
    if (write->complete && write->result != CURLE_OK) {
        snprintf(error, error_size, "HTTP upload failed: %s",
                 write->curl_error[0] ? write->curl_error :
                 curl_easy_strerror(write->result));
        write->failed = true;
        return -1;
    }
    return 0;
}

static int http_open_write(NavProvider *provider, const char *resource_id,
                           bool overwrite, uint64_t total, bool total_known,
                           void **handle, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    HttpWrite *write;
    if (!resource_id || !url_is_within_root(http, resource_id)) {
        snprintf(error, error_size, "HTTP upload destination is outside repository root");
        return -1;
    }
    if (total_known && total > (uint64_t)INT64_MAX) {
        snprintf(error, error_size, "upload size exceeds HTTP transport limit");
        return -1;
    }
    write = calloc(1, sizeof *write);
    if (!write) {
        snprintf(error, error_size, "out of memory opening HTTP upload");
        return -1;
    }
    write->kind = 2;
    write->provider = http;
    write->easy = curl_easy_init();
    write->multi = curl_multi_init();
    if (!write->easy || !write->multi) goto initialize_error;
    if (configure_request(http, write->easy, resource_id, &write->credential,
                          error, error_size))
        goto credential_error;
    curl_easy_setopt(write->easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(write->easy, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(write->easy, CURLOPT_READFUNCTION, upload_read);
    curl_easy_setopt(write->easy, CURLOPT_READDATA, write);
    curl_easy_setopt(write->easy, CURLOPT_WRITEFUNCTION, upload_response);
    curl_easy_setopt(write->easy, CURLOPT_WRITEDATA, write);
    curl_easy_setopt(write->easy, CURLOPT_HEADERFUNCTION, upload_header);
    curl_easy_setopt(write->easy, CURLOPT_HEADERDATA, write);
    curl_easy_setopt(write->easy, CURLOPT_ERRORBUFFER, write->curl_error);
    if (total_known)
        curl_easy_setopt(write->easy, CURLOPT_INFILESIZE_LARGE, (curl_off_t)total);
    write->headers = curl_slist_append(write->headers, "Expect:");
    if (!overwrite)
        write->headers = curl_slist_append(write->headers, "If-None-Match: *");
    if (!write->headers) goto initialize_error;
    curl_easy_setopt(write->easy, CURLOPT_HTTPHEADER, write->headers);
    if (curl_multi_add_handle(write->multi, write->easy) != CURLM_OK)
        goto initialize_error;
    *handle = write;
    return 0;
initialize_error:
    if (write->multi) curl_multi_cleanup(write->multi);
    if (write->easy) curl_easy_cleanup(write->easy);
    curl_slist_free_all(write->headers);
    nav_resolved_credential_free(&write->credential);
    free(write);
    snprintf(error, error_size, "unable to initialize HTTP upload");
    return -1;
credential_error:
    if (write->multi) curl_multi_cleanup(write->multi);
    if (write->easy) curl_easy_cleanup(write->easy);
    nav_resolved_credential_free(&write->credential);
    free(write);
    return -1;
}

static int upload_status_error(HttpWrite *write, char *error,
                               size_t error_size);

static int http_write(NavProvider *provider, void *handle, const void *buffer,
                      size_t length, char *error, size_t error_size)
{
    HttpWrite *write = handle;
    (void)provider;
    if (!write || write->kind != 2 || (!buffer && length) || write->failed ||
        write->finishing) {
        snprintf(error, error_size, "invalid HTTP upload state");
        return -1;
    }
    if (!length) return 0;
    write->input = buffer;
    write->input_length = length;
    write->input_offset = 0;
    if (write->paused) {
        write->paused = false;
        if (curl_easy_pause(write->easy, CURLPAUSE_CONT) != CURLE_OK) {
            snprintf(error, error_size, "unable to resume HTTP upload");
            write->failed = true;
            return -1;
        }
    }
    if (upload_drive(write, false, error, error_size)) return -1;
    if (write->complete && write->input_offset != write->input_length) {
        if (write->response_status < 200 || write->response_status >= 300)
            upload_status_error(write, error, error_size);
        else
            snprintf(error, error_size,
                     "HTTP server ended upload before accepting all data");
        write->failed = true;
        return -1;
    }
    write->input = NULL;
    write->input_length = write->input_offset = 0;
    return 0;
}

static int upload_status_error(HttpWrite *write, char *error,
                               size_t error_size)
{
    if (write->response_status >= 200 && write->response_status < 300) return 0;
    if (authentication_status_error(write->provider, write->response_status,
                                    error, error_size))
        return -1;
    if (write->response_status == 403)
        snprintf(error, error_size, "HTTP upload forbidden (403)");
    else if (write->response_status == 405)
        snprintf(error, error_size, "repository does not support upload (405)");
    else if (write->response_status == 413)
        snprintf(error, error_size, "HTTP upload is too large (413)");
    else if (write->response_status == 412)
        snprintf(error, error_size, "remote destination already exists (412)");
    else
        snprintf(error, error_size, "HTTP upload returned status %ld",
                 write->response_status);
    return -1;
}

static int http_close_write(HttpWrite *write, char *error, size_t error_size)
{
    int result = write->failed ? -1 : 0;
    if (!write->failed) {
        write->finishing = true;
        if (write->paused) {
            write->paused = false;
            if (curl_easy_pause(write->easy, CURLPAUSE_CONT) != CURLE_OK) {
                snprintf(error, error_size, "unable to finish HTTP upload");
                result = -1;
            }
        }
        if (!result && upload_drive(write, true, error, error_size)) result = -1;
        if (!result && upload_status_error(write, error, error_size)) result = -1;
        if (!result) {
            char *effective = NULL;
            curl_easy_getinfo(write->easy, CURLINFO_EFFECTIVE_URL, &effective);
            if (!effective || !url_is_within_root(write->provider, effective)) {
                snprintf(error, error_size, "HTTP upload left repository root");
                result = -1;
            }
        }
    }
    curl_multi_remove_handle(write->multi, write->easy);
    curl_multi_cleanup(write->multi);
    curl_easy_cleanup(write->easy);
    curl_slist_free_all(write->headers);
    nav_resolved_credential_free(&write->credential);
    free(write);
    return result;
}

static int http_close(NavProvider *provider, void *handle, char *error,
                      size_t error_size)
{
    int kind = handle ? *(int *)handle : 0;
    if (kind == 1)
        return http_close_read(provider, handle, error, error_size);
    if (kind == 2)
        return http_close_write(handle, error, error_size);
    snprintf(error, error_size, "invalid HTTP handle");
    return -1;
}

static bool http_write_started(NavProvider *provider, void *handle)
{
    HttpWrite *write = handle;
    (void)provider;
    return write && write->kind == 2 && write->bytes_sent != 0;
}

static int http_mutation(NavProvider *provider, const char *resource_id,
                         const char *method, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    CURL *easy;
    CURLcode code;
    long status = 0;
    NavResolvedCredential credential = {0};
    char curl_error[CURL_ERROR_SIZE] = {0};
    if (!resource_id || !url_is_within_root(http, resource_id) ||
        !strcmp(resource_id, http->root)) {
        snprintf(error, error_size, "remote mutation is outside repository root");
        return -1;
    }
    easy = curl_easy_init();
    if (!easy) {
        snprintf(error, error_size, "unable to initialize HTTP %s", method);
        return -1;
    }
    if (configure_request(http, easy, resource_id, &credential,
                          error, error_size)) {
        curl_easy_cleanup(easy);
        return -1;
    }
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, upload_response);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, curl_error);
    code = curl_easy_perform(easy);
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(easy);
    nav_resolved_credential_free(&credential);
    if (code != CURLE_OK) {
        snprintf(error, error_size, "HTTP %s failed: %s", method,
                 curl_error[0] ? curl_error : curl_easy_strerror(code));
        return -1;
    }
    if (status >= 200 && status < 300) return 0;
    if (authentication_status_error(http, status, error, error_size)) return -1;
    if (!strcmp(method, "MKCOL")) {
        if (status == 400) snprintf(error, error_size, "invalid directory request (400)");
        else if (status == 403) snprintf(error, error_size, "directory creation forbidden (403)");
        else if (status == 405) snprintf(error, error_size, "directory already exists or creation unsupported (405)");
        else if (status == 409) snprintf(error, error_size, "directory parent does not exist or conflicts (409)");
        else if (status >= 500) snprintf(error, error_size, "directory server error (%ld)", status);
        else snprintf(error, error_size, "directory creation returned status %ld", status);
    } else {
        if (status == 403) snprintf(error, error_size, "delete forbidden (403)");
        else if (status == 404) snprintf(error, error_size, "remote entry no longer exists (404)");
        else if (status == 405) snprintf(error, error_size, "delete unsupported (405)");
        else if (status == 409) snprintf(error, error_size, "directory is not empty or conflicts (409)");
        else if (status >= 500) snprintf(error, error_size, "delete server error (%ld)", status);
        else snprintf(error, error_size, "delete returned status %ld", status);
    }
    return -1;
}

static int http_mkdir(NavProvider *provider, const char *resource_id,
                      char *error, size_t error_size)
{
    return http_mutation(provider, resource_id, "MKCOL", error, error_size);
}

static int http_remove(NavProvider *provider, const char *resource_id,
                       char *error, size_t error_size)
{
    return http_mutation(provider, resource_id, "DELETE", error, error_size);
}

static int http_rename(NavProvider *provider, const char *source,
                       const char *destination, char *error,
                       size_t error_size)
{
    HttpProvider *http = provider->context;
    CURL *easy = NULL;
    struct curl_slist *headers = NULL;
    struct curl_slist *next;
    CURLcode code;
    long status = 0;
    char destination_header[NAV_URL_MAX + 32];
    char curl_error[CURL_ERROR_SIZE] = {0};
    NavResolvedCredential credential = {0};
    int result = -1;
    if (!source || !destination || !url_is_within_root(http, source) ||
        !url_is_within_root(http, destination) || !strcmp(source, http->root) ||
        !strcmp(destination, http->root) || !strcmp(source, destination)) {
        snprintf(error, error_size, "rename is outside repository root");
        return -1;
    }
    if (snprintf(destination_header, sizeof destination_header,
                 "Destination: %s", destination) >=
        (int)sizeof destination_header) {
        snprintf(error, error_size, "rename destination is too long");
        return -1;
    }
    easy = curl_easy_init();
    headers = curl_slist_append(NULL, destination_header);
    next = headers ? curl_slist_append(headers, "Overwrite: F") : NULL;
    if (next) headers = next;
    if (!easy || !headers || !next) {
        snprintf(error, error_size, "unable to initialize HTTP MOVE");
        goto finish;
    }
    if (configure_request(http, easy, source, &credential, error, error_size))
        goto finish;
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "MOVE");
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, upload_response);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, curl_error);
    code = curl_easy_perform(easy);
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    if (code != CURLE_OK) {
        snprintf(error, error_size, "HTTP MOVE failed: %s",
                 curl_error[0] ? curl_error : curl_easy_strerror(code));
        goto finish;
    }
    if (status >= 200 && status < 300) {
        result = 0;
        goto finish;
    }
    if (authentication_status_error(http, status, error, error_size))
        goto finish;
    if (status == 400) snprintf(error, error_size, "invalid move request (400)");
    else if (status == 403) snprintf(error, error_size, "rename/move forbidden (403)");
    else if (status == 404) snprintf(error, error_size, "source no longer exists (404)");
    else if (status == 405) snprintf(error, error_size, "rename/move unsupported (405)");
    else if (status == 409) snprintf(error, error_size, "rename destination conflicts (409)");
    else if (status == 412) snprintf(error, error_size, "rename destination already exists (412)");
    else if (status == 423) snprintf(error, error_size, "remote resource is locked (423)");
    else if (status >= 500) snprintf(error, error_size, "rename server error (%ld)", status);
    else snprintf(error, error_size, "rename returned status %ld", status);
finish:
    curl_slist_free_all(headers);
    if (easy) curl_easy_cleanup(easy);
    nav_resolved_credential_free(&credential);
    return result;
}

static size_t range_header(char *data, size_t size, size_t count, void *userdata)
{
    RangeResponse *response = userdata;
    size_t amount;
    if (size && count > SIZE_MAX / size) return 0;
    amount = size * count;
    if (amount >= 5 && !strncasecmp(data, "HTTP/", 5)) {
        long status = 0;
        if (sscanf(data, "HTTP/%*s %ld", &status) == 1) response->status = status;
    } else if (amount > 14 && !strncasecmp(data, "Content-Range:", 14)) {
        unsigned long long start, end, total;
        if (sscanf(data + 14, " bytes %llu-%llu/%llu", &start, &end, &total) == 3) {
            response->range_start = (uint64_t)start;
            response->total = (uint64_t)total;
            response->content_range_known = true;
        }
    }
    return amount;
}

static size_t range_data(char *data, size_t size, size_t count, void *userdata)
{
    RangeResponse *response = userdata;
    size_t amount;
    if (size && count > SIZE_MAX / size) return 0;
    amount = size * count;
    if (response->status != 200 && response->status != 206) return amount;
    if (amount > response->capacity - response->length) {
        response->exceeded = true;
        return 0;
    }
    memcpy(response->data + response->length, data, amount);
    response->length += amount;
    return amount;
}

static int http_read_at(NavProvider *provider, const char *resource_id,
                        uint64_t offset, void *buffer, size_t capacity,
                        size_t *got, bool *eof, uint64_t *total,
                        bool *total_known, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    RangeResponse response = {.data = buffer, .capacity = capacity};
    CURL *easy;
    CURLcode code;
    char range[96], *effective = NULL;
    curl_off_t content_length = -1;
    uint64_t end;
    NavResolvedCredential credential = {0};
    if (!resource_id || !buffer || !got || !eof || !total || !total_known ||
        !capacity || !url_is_within_root(http, resource_id)) {
        snprintf(error, error_size, "invalid bounded HTTP read");
        return -1;
    }
    end = offset > UINT64_MAX - (uint64_t)capacity + 1
              ? UINT64_MAX : offset + (uint64_t)capacity - 1;
    snprintf(range, sizeof range, "%llu-%llu",
             (unsigned long long)offset, (unsigned long long)end);
    easy = curl_easy_init();
    if (!easy) {
        snprintf(error, error_size, "unable to initialize bounded HTTP read");
        return -1;
    }
    if (configure_request(http, easy, resource_id, &credential,
                          error, error_size)) {
        curl_easy_cleanup(easy);
        return -1;
    }
    curl_easy_setopt(easy, CURLOPT_RANGE, range);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, range_data);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, range_header);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &response);
    code = curl_easy_perform(easy);
    curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective);
    curl_easy_getinfo(easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
    if (code != CURLE_OK) {
        if (response.status == 200 && response.exceeded)
            snprintf(error, error_size,
                     "server does not support bounded Range viewing");
        else
            snprintf(error, error_size, "HTTP range read failed: %s",
                     curl_easy_strerror(code));
        curl_easy_cleanup(easy);
        nav_resolved_credential_free(&credential);
        return -1;
    }
    if (!effective || !url_is_within_root(http, effective)) {
        snprintf(error, error_size, "HTTP redirect left repository root");
        curl_easy_cleanup(easy);
        nav_resolved_credential_free(&credential);
        return -1;
    }
    if (response.status == 206) {
        if (!response.content_range_known || response.range_start != offset) {
            snprintf(error, error_size, "malformed HTTP range response");
            curl_easy_cleanup(easy);
            nav_resolved_credential_free(&credential);
            return -1;
        }
        *total = response.total;
        *total_known = true;
        *eof = offset + response.length >= response.total;
    } else if (response.status == 200 && offset == 0 && !response.exceeded) {
        *total_known = content_length >= 0;
        *total = content_length >= 0 ? (uint64_t)content_length : response.length;
        *eof = true;
    } else {
        if (!authentication_status_error(http, response.status,
                                         error, error_size))
            snprintf(error, error_size,
                     "HTTP server returned status %ld for Range",
                     response.status);
        curl_easy_cleanup(easy);
        nav_resolved_credential_free(&credential);
        return -1;
    }
    *got = response.length;
    curl_easy_cleanup(easy);
    nav_resolved_credential_free(&credential);
    return 0;
}

typedef struct { time_t modified; bool known; } HttpStatDate;
static size_t stat_date_header(char *data, size_t size, size_t count, void *userdata)
{
    HttpStatDate *date = userdata;
    if (size && count > SIZE_MAX / size) return 0;
    size_t length = size * count;
    if (length >= 5 && !memcmp(data, "HTTP/", 5)) date->known = false;
    if (length > 14 && !strncasecmp(data, "Last-Modified:", 14)) {
        char value[128];
        size_t bytes = length - 14;
        if (bytes < sizeof value) {
            memcpy(value, data + 14, bytes); value[bytes] = 0;
            time_t modified = curl_getdate(value, NULL);
            date->known = modified != (time_t)-1;
            date->modified = modified;
        }
    }
    return length;
}
static int http_stat(NavProvider *provider, const char *resource_id,
                     NavEntry *entry, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    CURL *easy;
    CURLcode code;
    long status = 0;
    curl_off_t length = -1;
    HttpStatDate date = {0};
    char *effective = NULL;
    NavResolvedCredential credential = {0};
    if (!resource_id || !entry || !url_is_within_root(http, resource_id)) {
        snprintf(error, error_size, "invalid HTTP resource");
        return -1;
    }
    easy = curl_easy_init();
    if (!easy) return -1;
    if (configure_request(http, easy, resource_id, &credential,
                          error, error_size)) {
        curl_easy_cleanup(easy);
        return -1;
    }
    curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 10L);
    /* Validate the actual header: some curl versions map malformed dates to
     * epoch zero in FILETIME_T, which must not invent known metadata. */
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, stat_date_header);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &date);
    code = curl_easy_perform(easy);
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective);
    if (code != CURLE_OK || status < 200 || status >= 300 || !effective ||
        !url_is_within_root(http, effective)) {
        if (code != CURLE_OK)
            snprintf(error, error_size, "HTTP metadata failed: %s",
                     curl_easy_strerror(code));
        else if (!authentication_status_error(http, status,
                                              error, error_size))
            snprintf(error, error_size, "HTTP metadata returned status %ld",
                     status);
        curl_easy_cleanup(easy);
        nav_resolved_credential_free(&credential);
        return -1;
    }
    memset(entry, 0, sizeof *entry);
    snprintf(entry->resource_id, sizeof entry->resource_id, "%s", resource_id);
    const char *leaf = strrchr(resource_id, '/');
    snprintf(entry->name, sizeof entry->name, "%s", leaf ? leaf + 1 : resource_id);
    /* Autoindex servers commonly redirect /dir to /dir/. HEAD reveals that
       directory intent without downloading a listing or guessing extensions. */
    CURLU *effective_url = curl_url(); char *effective_path = NULL;
    if (effective_url && !curl_url_set(effective_url, CURLUPART_URL, effective, 0) &&
        !curl_url_get(effective_url, CURLUPART_PATH, &effective_path, 0)) {
        size_t path_length = strlen(effective_path);
        if (path_length && effective_path[path_length - 1] == '/') entry->flags |= NAV_ENTRY_DIR;
    }
    curl_free(effective_path); curl_url_cleanup(effective_url);
    if (length >= 0) {
        entry->size = (uint64_t)length;
        entry->flags |= NAV_ENTRY_SIZE_KNOWN;
    }
    if (date.known) {
        entry->modified = date.modified;
        entry->flags |= NAV_ENTRY_MODIFIED_KNOWN;
    }
    curl_easy_cleanup(easy);
    nav_resolved_credential_free(&credential);
    return 0;
}

static int http_list_progress(NavProvider *provider, const char *resource_id, bool hidden,
                     NavListing *listing, const NavListOptions *options, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    NavListing pending = {0};
    HttpListingParser parser = {.http = http, .listing = &pending,
                                .transport = http->easy, .options = options};
    CURLcode code;
    long status = 0;
    char *effective = NULL;
    NavResolvedCredential credential = {0};
    int result = -1;
    (void)hidden;
    curl_easy_reset(http->easy);
    if (configure_request(http, http->easy, resource_id, &credential,
                          error, error_size))
        goto finish;
    curl_easy_setopt(http->easy, CURLOPT_WRITEFUNCTION, receive_listing_data);
    curl_easy_setopt(http->easy, CURLOPT_WRITEDATA, &parser);
    curl_easy_setopt(http->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(http->easy, CURLOPT_XFERINFOFUNCTION, listing_progress);
    curl_easy_setopt(http->easy, CURLOPT_XFERINFODATA, &parser);
    curl_easy_setopt(http->easy, CURLOPT_TIMEOUT, 10L);
    code = curl_easy_perform(http->easy);
    curl_easy_getinfo(http->easy, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(http->easy, CURLINFO_EFFECTIVE_URL, &effective);
    if (parser.memory_error || parser.oversized || parser.outside_root) {
        listing_error(&parser, error, error_size);
        goto finish;
    }
    if (code != CURLE_OK) {
        if (parser.cancelled) snprintf(error, error_size, "Repository listing cancelled");
        else if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CACERT_BADFILE)
            snprintf(error, error_size, "TLS verification failed: %s",
                     curl_easy_strerror(code));
        else
            snprintf(error, error_size, "HTTP request failed: %s",
                     curl_easy_strerror(code));
        goto finish;
    }
    if (status < 200 || status >= 300) {
        if (!authentication_status_error(http, status, error, error_size))
            snprintf(error, error_size, "HTTP server returned status %ld", status);
        goto finish;
    }
    if (!effective || !url_is_within_root(http, effective)) {
        snprintf(error, error_size, "HTTP redirect left repository root");
        goto finish;
    }
    if (!parser.started && listing_start(&parser, effective)) {
        parser.memory_error = true;
        listing_error(&parser, error, error_size);
        goto finish;
    }
    listing_finish_row(&parser);
    parser.progress.entries = parser.used;
    if (options && options->progress) options->progress(&parser.progress, options->userdata);
    nav_listing_free(listing);
    *listing = pending;
    memset(&pending, 0, sizeof pending);
    result = 0;
finish:
    free(parser.slots);
    nav_listing_free(&pending);
    nav_resolved_credential_free(&credential);
    return result;
}
static int http_list(NavProvider *provider, const char *resource_id, bool hidden,
                     NavListing *listing, char *error, size_t error_size)
{ return http_list_progress(provider, resource_id, hidden, listing, NULL, error, error_size); }

static void http_destroy(NavProvider *provider)
{
    HttpProvider *http = provider ? provider->context : NULL;
    if (!http) return;
    curl_easy_cleanup(http->easy);
    free(http);
}

NavProvider *nav_http_provider_create(const NavRepository *repository,
                                      NavCredentialStore *credential_store,
                                      char *error, size_t error_size)
{
    HttpProvider *http;
    char normalized[NAV_URL_MAX];
    if (!repository || !repository->name[0] ||
        nav_repository_normalize_url(repository ? repository->url : NULL,
                                     normalized, sizeof normalized,
                                     error, error_size))
        return NULL;
    if (repository->credential[0] && strncasecmp(normalized, "https://", 8)) {
        snprintf(error, error_size,
                 "credentialed repositories require HTTPS");
        return NULL;
    }
    if (ensure_curl(error, error_size)) return NULL;
    http = calloc(1, sizeof *http);
    if (!http) {
        snprintf(error, error_size, "out of memory creating HTTP provider");
        return NULL;
    }
    http->easy = curl_easy_init();
    if (!http->easy || resolve_url(normalized, NULL, http->root,
                                   sizeof http->root, error, error_size)) {
        if (http->easy) curl_easy_cleanup(http->easy);
        free(http);
        return NULL;
    }
    if (http->root[strlen(http->root) - 1] != '/') {
        size_t length = strlen(http->root);
        if (length + 1 >= sizeof http->root) {
            snprintf(error, error_size, "repository URL is too long");
            curl_easy_cleanup(http->easy);
            free(http);
            return NULL;
        }
        http->root[length] = '/'; http->root[length + 1] = 0;
    }
    snprintf(http->name, sizeof http->name, "%s", repository->name);
    snprintf(http->credential_name, sizeof http->credential_name, "%s",
             repository->credential);
    http->credential_store = credential_store;
    http->tls_verify = repository->tls_verify;
    http->provider.scheme = "http";
    http->provider.display_name = http->name;
    http->provider.capabilities = NAV_CAP_LIST | NAV_CAP_READ | NAV_CAP_STAT |
                                  NAV_CAP_RANDOM_READ;
    if (repository->writable) http->provider.capabilities |= NAV_CAP_WRITE;
    if (repository->mkdir_enabled) http->provider.capabilities |= NAV_CAP_MKDIR;
    if (repository->delete_enabled) http->provider.capabilities |= NAV_CAP_DELETE;
    if (repository->rename_enabled) http->provider.capabilities |= NAV_CAP_RENAME;
    http->provider.context = http;
    http->provider.destroy = http_destroy;
    http->provider.location = http_location;
    http->provider.location_child = http_child;
    http->provider.location_parent = http_parent;
    http->provider.list = http_list;
    http->provider.list_progress = http_list_progress;
    http->provider.remove = repository->delete_enabled ? http_remove : NULL;
    http->provider.mkdir = repository->mkdir_enabled ? http_mkdir : NULL;
    http->provider.rename_path = repository->rename_enabled ? http_rename : NULL;
    http->provider.stat = http_stat;
    http->provider.open_read = http_open_read;
    http->provider.open_write = repository->writable ? http_open_write : NULL;
    http->provider.read = http_read;
    http->provider.read_cancellable = http_read_cancellable;
    http->provider.read_at = http_read_at;
    http->provider.write = repository->writable ? http_write : NULL;
    http->provider.write_started = repository->writable ? http_write_started : NULL;
    http->provider.close = http_close;
    return &http->provider;
}
