#include "nav.h"
#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HTTP_LISTING_LIMIT (4u * 1024u * 1024u)

typedef struct
{
    NavProvider provider;
    CURL *easy;
    char name[NAV_REPO_NAME_MAX];
    char root[NAV_URL_MAX];
    char credential_name[NAV_CREDENTIAL_NAME_MAX];
    NavCredentialStore *credential_store;
    bool tls_verify;
} HttpProvider;

typedef struct
{
    char *data;
    size_t length, capacity;
    bool exceeded;
} HttpResponse;

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
    bool paused, complete, failed;
    CURLcode result;
    char curl_error[CURL_ERROR_SIZE];
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
        NavEntry *items = realloc(listing->items, capacity * sizeof *items);
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
    curl_easy_setopt(easy, CURLOPT_URL, resource_id);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "Navi8or/0.1");
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR,
                     http->credential_name[0] ? "https" : "http,https");
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

static const char *find_case(const char *start, const char *needle)
{
    size_t length = strlen(needle);
    for (const char *cursor = start; *cursor; cursor++)
        if (!strncasecmp(cursor, needle, length)) return cursor;
    return NULL;
}

static bool listing_contains(const NavListing *listing, const char *resource_id)
{
    for (size_t index = 0; index < listing->count; index++)
        if (!strcmp(listing->items[index].resource_id, resource_id)) return true;
    return false;
}

static int add_link(HttpProvider *http, const char *current, const char *href,
                    NavListing *listing)
{
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
        !url_is_within_root(http, resolved) || !strcmp(resolved, current) ||
        listing_contains(listing, resolved))
        return 0;
    directory = href[strlen(href) - 1] == '/';
    url = curl_url();
    if (!url || curl_url_set(url, CURLUPART_URL, resolved, 0) ||
        curl_url_get(url, CURLUPART_PATH, &url_path, 0)) {
        curl_url_cleanup(url);
        return 0;
    }
    snprintf(path, sizeof path, "%s", url_path);
    curl_free(url_path);
    curl_url_cleanup(url);
    length = strlen(path);
    while (length > 1 && path[length - 1] == '/') path[--length] = 0;
    const char *leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    decoded = curl_easy_unescape(http->easy, leaf, 0, NULL);
    if (!decoded || !decoded[0] || !strcmp(decoded, ".") ||
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
    return listing_append(listing, &entry);
}

int nav_http_parse_directory_html(NavProvider *provider, const char *current,
                                  const char *html, NavListing *listing,
                                  char *error, size_t error_size)
{
    HttpProvider *http;
    NavEntry parent = {0};
    const char *cursor;
    if (!provider || !provider->context || !current || !html || !listing) {
        snprintf(error, error_size, "invalid HTTP directory listing");
        return -1;
    }
    http = provider->context;
    nav_listing_free(listing);
    snprintf(parent.name, sizeof parent.name, "..");
    snprintf(parent.resource_id, sizeof parent.resource_id, "%s", current);
    parent.flags = NAV_ENTRY_DIR | NAV_ENTRY_PARENT;
    if (listing_append(listing, &parent)) goto memory_error;
    cursor = html;
    while ((cursor = find_case(cursor, "<a"))) {
        const char *tag_end = strchr(cursor, '>'), *attribute;
        char href[NAV_URL_MAX] = {0};
        if (!tag_end) break;
        attribute = cursor + 2;
        while (attribute < tag_end) {
            const char *name_start, *value_start;
            size_t name_length, value_length;
            char quote = 0;
            while (attribute < tag_end && isspace((unsigned char)*attribute)) attribute++;
            name_start = attribute;
            while (attribute < tag_end &&
                   (isalnum((unsigned char)*attribute) || *attribute == '-' ||
                    *attribute == '_')) attribute++;
            name_length = (size_t)(attribute - name_start);
            while (attribute < tag_end && isspace((unsigned char)*attribute)) attribute++;
            if (attribute >= tag_end || *attribute != '=') {
                if (attribute == name_start) attribute++;
                continue;
            }
            attribute++;
            while (attribute < tag_end && isspace((unsigned char)*attribute)) attribute++;
            if (attribute < tag_end && (*attribute == '"' || *attribute == '\''))
                quote = *attribute++;
            value_start = attribute;
            while (attribute < tag_end &&
                   (quote ? *attribute != quote : !isspace((unsigned char)*attribute)))
                attribute++;
            value_length = (size_t)(attribute - value_start);
            if (quote && attribute < tag_end) attribute++;
            if (name_length == 4 && !strncasecmp(name_start, "href", 4)) {
                if (value_length >= sizeof href) value_length = sizeof href - 1;
                memcpy(href, value_start, value_length);
                href[value_length] = 0;
                break;
            }
        }
        if (href[0] && add_link(http, current, href, listing)) goto memory_error;
        cursor = tag_end + 1;
    }
    return 0;
memory_error:
    nav_listing_free(listing);
    snprintf(error, error_size, "out of memory parsing HTTP directory listing");
    return -1;
}

static size_t receive_data(char *data, size_t size, size_t count, void *userdata)
{
    HttpResponse *response = userdata;
    size_t amount;
    if (size && count > SIZE_MAX / size) {
        response->exceeded = true;
        return 0;
    }
    amount = size * count;
    if (amount > HTTP_LISTING_LIMIT - response->length) {
        response->exceeded = true;
        return 0;
    }
    if (response->length + amount + 1 > response->capacity) {
        size_t capacity = response->capacity ? response->capacity * 2 : 16384;
        while (capacity < response->length + amount + 1) capacity *= 2;
        char *next = realloc(response->data, capacity);
        if (!next) return 0;
        response->data = next;
        response->capacity = capacity;
    }
    memcpy(response->data + response->length, data, amount);
    response->length += amount;
    response->data[response->length] = 0;
    return amount;
}

static size_t read_header(char *data, size_t size, size_t count, void *userdata)
{
    HttpRead *read = userdata;
    size_t amount;
    if (size && count > SIZE_MAX / size) return 0;
    amount = size * count;
    if (amount >= 5 && !strncasecmp(data, "HTTP/", 5)) {
        long status = 0;
        if (sscanf(data, "HTTP/%*s %ld", &status) == 1)
            read->response_status = status;
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
        curl_multi_cleanup(read->multi);
        curl_easy_cleanup(read->easy);
        free(read);
        return -1;
    }
    curl_easy_setopt(read->easy, CURLOPT_WRITEFUNCTION, stream_data);
    curl_easy_setopt(read->easy, CURLOPT_WRITEDATA, read);
    curl_easy_setopt(read->easy, CURLOPT_HEADERFUNCTION, read_header);
    curl_easy_setopt(read->easy, CURLOPT_HEADERDATA, read);
    curl_easy_setopt(read->easy, CURLOPT_ERRORBUFFER, read->curl_error);
    if (curl_multi_add_handle(read->multi, read->easy) != CURLM_OK) {
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

static int http_read(NavProvider *provider, void *handle, void *buffer,
                     size_t capacity, size_t *got, char *error,
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
        multi_code = curl_multi_perform(read->multi, &running);
        if (multi_code != CURLM_OK) {
            snprintf(error, error_size, "HTTP read failed: %s",
                     curl_multi_strerror(multi_code));
            read->failed = true;
            return -1;
        }
        read_completion(read);
        if (read->target_used == capacity || read->paused || read->complete) break;
        if (running) {
            multi_code = curl_multi_poll(read->multi, NULL, 0, 1000, NULL);
            if (multi_code != CURLM_OK) {
                snprintf(error, error_size, "HTTP read failed: %s",
                         curl_multi_strerror(multi_code));
                read->failed = true;
                return -1;
            }
        }
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

static int http_stat(NavProvider *provider, const char *resource_id,
                     NavEntry *entry, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    CURL *easy;
    CURLcode code;
    long status = 0;
    curl_off_t length = -1;
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
    if (length >= 0) {
        entry->size = (uint64_t)length;
        entry->flags |= NAV_ENTRY_SIZE_KNOWN;
    }
    curl_easy_cleanup(easy);
    nav_resolved_credential_free(&credential);
    return 0;
}

static int http_list(NavProvider *provider, const char *resource_id, bool hidden,
                     NavListing *listing, char *error, size_t error_size)
{
    HttpProvider *http = provider->context;
    HttpResponse response = {0};
    CURLcode code;
    long status = 0;
    char *effective = NULL;
    NavResolvedCredential credential = {0};
    (void)hidden;
    curl_easy_reset(http->easy);
    if (configure_request(http, http->easy, resource_id, &credential,
                          error, error_size))
        return -1;
    curl_easy_setopt(http->easy, CURLOPT_WRITEFUNCTION, receive_data);
    curl_easy_setopt(http->easy, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(http->easy, CURLOPT_TIMEOUT, 10L);
    code = curl_easy_perform(http->easy);
    curl_easy_getinfo(http->easy, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(http->easy, CURLINFO_EFFECTIVE_URL, &effective);
    if (code != CURLE_OK) {
        if (response.exceeded)
            snprintf(error, error_size, "HTTP directory listing exceeds 4 MB");
        else if (code == CURLE_PEER_FAILED_VERIFICATION ||
                 code == CURLE_SSL_CACERT_BADFILE)
            snprintf(error, error_size, "TLS verification failed: %s",
                     curl_easy_strerror(code));
        else
            snprintf(error, error_size, "HTTP request failed: %s",
                     curl_easy_strerror(code));
        free(response.data);
        nav_resolved_credential_free(&credential);
        return -1;
    }
    if (status < 200 || status >= 300) {
        if (!authentication_status_error(http, status, error, error_size))
            snprintf(error, error_size, "HTTP server returned status %ld", status);
        free(response.data);
        nav_resolved_credential_free(&credential);
        return -1;
    }
    if (!effective || !url_is_within_root(http, effective)) {
        snprintf(error, error_size, "HTTP redirect left repository root");
        free(response.data);
        nav_resolved_credential_free(&credential);
        return -1;
    }
    if (!response.data) response.data = calloc(1, 1);
    if (!response.data) {
        snprintf(error, error_size, "out of memory receiving HTTP listing");
        nav_resolved_credential_free(&credential);
        return -1;
    }
    int result = nav_http_parse_directory_html(provider, effective, response.data,
                                                listing, error, error_size);
    free(response.data);
    nav_resolved_credential_free(&credential);
    return result;
}

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
    http->provider.remove = repository->delete_enabled ? http_remove : NULL;
    http->provider.mkdir = repository->mkdir_enabled ? http_mkdir : NULL;
    http->provider.rename_path = repository->rename_enabled ? http_rename : NULL;
    http->provider.stat = http_stat;
    http->provider.open_read = http_open_read;
    http->provider.open_write = repository->writable ? http_open_write : NULL;
    http->provider.read = http_read;
    http->provider.read_at = http_read_at;
    http->provider.write = repository->writable ? http_write : NULL;
    http->provider.write_started = repository->writable ? http_write_started : NULL;
    http->provider.close = http_close;
    return &http->provider;
}
