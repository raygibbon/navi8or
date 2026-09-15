#include "nav_smb.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/smb2-errors.h>

typedef struct SmbRead SmbRead;
typedef struct
{
    NavProvider provider;
    NavSmbUrl root;
    char name[NAV_REPO_NAME_MAX];
    char credential_name[NAV_CREDENTIAL_NAME_MAX];
    NavCredentialStore *credentials; /* Borrowed; the application owns the vault. */
    SmbRead *reads;
} SmbProvider;

struct SmbRead
{
    struct smb2_context *context;
    struct smb2fh *file;
    SmbRead *next;
    bool failed;
};

/* Never forward library error strings: they may include authentication details. */
static int smb_error(struct smb2_context *context, int code, const char *operation,
                      char *error, size_t error_size)
{
    uint32_t status = (uint32_t)smb2_get_nterror(context);
    const char *message = "network connection failed or was interrupted";
    switch (status) {
    case SMB2_STATUS_LOGON_FAILURE: message = "authentication failed"; break;
    case SMB2_STATUS_ACCESS_DENIED:
    case SMB2_STATUS_NETWORK_ACCESS_DENIED: message = "access denied"; break;
    case SMB2_STATUS_BAD_NETWORK_NAME: message = "share does not exist"; break;
    case SMB2_STATUS_OBJECT_NAME_NOT_FOUND:
    case SMB2_STATUS_OBJECT_PATH_NOT_FOUND: message = "file or directory does not exist"; break;
    case SMB2_STATUS_IO_TIMEOUT: message = "server timed out"; break;
    case SMB2_STATUS_STOPPED_ON_SYMLINK: message = "symbolic links are not supported"; break;
    default:
        if (code == -ENOMEM) message = "out of memory";
        else if (code == -EACCES) message = "access denied";
        else if (code == -ENOENT) message = "file or directory does not exist";
        else {
            const char *detail = smb2_get_error(context);
            if (detail && (strstr(detail, "getaddrinfo") || strstr(detail, "resolve")))
                message = "server name could not be resolved";
        }
        break;
    }
    snprintf(error, error_size, "SMB %s: %s", operation, message);
    return -1;
}

static void session_close(struct smb2_context *context)
{
    if (!context) return;
    smb2_disconnect_share(context);
    smb2_destroy_context(context);
}

static struct smb2_context *session_open(SmbProvider *smb, char *error, size_t error_size)
{
    NavResolvedCredential credential = {0};
    struct smb2_context *context = NULL;
    const char *user = "guest", *password = "";
    if (smb->credential_name[0]) {
        if (!smb->credentials) {
            snprintf(error, error_size, "credential store is unavailable");
            return NULL;
        }
        if (nav_credential_store_resolve(smb->credentials, smb->credential_name,
                                         &credential, error, error_size)) goto finish;
        if (credential.type != NAV_CREDENTIAL_BASIC || !credential.username ||
            !credential.username[0] || !credential.secret) {
            snprintf(error, error_size, "SMB requires a username/password credential");
            goto finish;
        }
        user = credential.username;
        password = credential.secret;
    }
    context = smb2_init_context();
    if (!context) {
        snprintf(error, error_size, "unable to allocate SMB connection");
        goto finish;
    }
    smb2_set_timeout(context, 10);
    smb2_set_authentication(context, SMB2_SEC_NTLMSSP);
    /* Domain can later be supplied here from credential metadata without an API
       change. Explicit identity/password override libsmb2's process defaults. */
    smb2_set_domain(context, "");
    smb2_set_user(context, user);
    smb2_set_password(context, password);
    if (smb->credential_name[0]) smb2_set_sign(context, 1);
    int result = smb2_connect_share(context, smb->root.server, smb->root.share, user);
    /* libsmb2 copies password; remove its copy as soon as authentication ends.
       The resolved vault copies below are always wiped by Navi8or's helper. */
    smb2_set_password(context, NULL);
    if (result) {
        smb_error(context, result, "connect", error, error_size);
        smb2_destroy_context(context);
        context = NULL;
    }
finish:
    nav_resolved_credential_free(&credential);
    return context;
}

static int target_url(SmbProvider *smb, const char *resource, NavSmbUrl *target,
                       char *error, size_t error_size)
{
    if (nav_smb_url_parse(resource, target, error, error_size)) return -1;
    if (!nav_smb_url_within(&smb->root, target)) {
        snprintf(error, error_size, "SMB location is outside repository root");
        return -1;
    }
    return 0;
}

static void set_location(SmbProvider *smb, const NavSmbUrl *url, NavLocation *location)
{
    size_t length = strlen(smb->root.path);
    memset(location, 0, sizeof *location);
    location->provider = &smb->provider;
    snprintf(location->resource_id, sizeof location->resource_id, "%s", url->url);
    const char *relative = url->path + length;
    if (*relative == '/') relative++;
    snprintf(location->display_path, sizeof location->display_path, "/%s", relative);
}

static int smb_location(NavProvider *provider, const char *input, NavLocation *out,
                         char *error, size_t error_size)
{
    SmbProvider *smb = provider->context;
    NavSmbUrl target;
    char url[NAV_URL_MAX];
    if (!input || !input[0]) input = smb->root.url;
    if (!strstr(input, "://")) {
        if (snprintf(url, sizeof url, "%s%s%s", smb->root.url,
                     smb->root.url[strlen(smb->root.url) - 1] == '/' ? "" : "/",
                     input[0] == '/' ? input + 1 : input) >= (int)sizeof url) {
            snprintf(error, error_size, "SMB location is too long");
            return -1;
        }
        input = url;
    }
    if (target_url(smb, input, &target, error, error_size)) return -1;
    set_location(smb, &target, out);
    return 0;
}

static int smb_child(NavProvider *provider, const NavLocation *parent, const char *name,
                      NavLocation *out, char *error, size_t error_size)
{
    SmbProvider *smb = provider->context;
    NavSmbUrl base, child;
    if (!parent || parent->provider != provider) {
        snprintf(error, error_size, "invalid SMB parent location");
        return -1;
    }
    if (target_url(smb, parent->resource_id, &base, error, error_size) ||
        nav_smb_url_child(&base, name, &child, error, error_size)) return -1;
    set_location(smb, &child, out);
    return 0;
}

static int smb_parent(NavProvider *provider, const NavLocation *location, NavLocation *out,
                       char *error, size_t error_size)
{
    SmbProvider *smb = provider->context;
    NavSmbUrl target;
    char url[NAV_URL_MAX];
    if (!location || location->provider != provider) {
        snprintf(error, error_size, "invalid SMB location");
        return -1;
    }
    if (target_url(smb, location->resource_id, &target, error, error_size)) return -1;
    if (!strcmp(target.path, smb->root.path)) {
        set_location(smb, &smb->root, out);
        return 0;
    }
    snprintf(url, sizeof url, "%s", target.url);
    char *slash = strrchr(url, '/');
    slash[1] = 0;
    return smb_location(provider, url, out, error, error_size);
}

/* Check all components, including ancestors of a subdirectory repository.
   SMB links/reparse points are deliberately not exposed as navigable entries. */
static int checked_stat(struct smb2_context *context, const NavSmbUrl *target,
                          struct smb2_stat_64 *st, char *error, size_t error_size)
{
    char path[NAV_URL_MAX];
    snprintf(path, sizeof path, "%s", target->path + 1);
    for (char *cursor = path;; cursor++) {
        if (*cursor && *cursor != '/') continue;
        char saved = *cursor;
        *cursor = 0;
        int result = smb2_stat(context, path, st);
        *cursor = saved;
        if (result) return smb_error(context, result, "stat", error, error_size);
        if (st->smb2_type != SMB2_TYPE_FILE && st->smb2_type != SMB2_TYPE_DIRECTORY) {
            snprintf(error, error_size, "SMB symbolic links and special files are not supported");
            return -1;
        }
        if (!saved) return 0;
        if (st->smb2_type != SMB2_TYPE_DIRECTORY) {
            snprintf(error, error_size, "SMB path component is not a directory");
            return -1;
        }
    }
}

static void entry_stat(NavEntry *entry, const struct smb2_stat_64 *st)
{
    entry->size = st->smb2_size;
    entry->modified = (time_t)st->smb2_mtime;
    entry->flags = NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN;
    if (st->smb2_type == SMB2_TYPE_DIRECTORY) entry->flags |= NAV_ENTRY_DIR;
}

static int append(NavListing *listing, const NavEntry *entry)
{
    if (listing->count == listing->capacity) {
        size_t capacity = listing->capacity ? listing->capacity * 2 : 32;
        if (capacity < listing->capacity || capacity > SIZE_MAX / sizeof *listing->items)
            return -1;
        NavEntry *items = realloc(listing->items, capacity * sizeof *items);
        if (!items) return -1;
        listing->items = items;
        listing->capacity = capacity;
    }
    listing->items[listing->count++] = *entry;
    return 0;
}

static int smb_list(NavProvider *provider, const char *resource, bool hidden,
                     NavListing *out, char *error, size_t error_size)
{
    SmbProvider *smb = provider->context;
    NavSmbUrl target, child;
    struct smb2_context *context;
    struct smb2dir *directory = NULL;
    struct smb2dirent *item;
    struct smb2_stat_64 st;
    NavListing listing = {0};
    NavEntry entry = {0};
    int result = -1;
    if (target_url(smb, resource, &target, error, error_size)) return -1;
    context = session_open(smb, error, error_size);
    if (!context) return -1;
    if (checked_stat(context, &target, &st, error, error_size)) goto finish;
    directory = smb2_opendir(context, target.path + 1);
    if (!directory) { smb_error(context, 0, "list", error, error_size); goto finish; }
    snprintf(entry.name, sizeof entry.name, "..");
    snprintf(entry.resource_id, sizeof entry.resource_id, "%s", target.url);
    entry.flags = NAV_ENTRY_DIR | NAV_ENTRY_PARENT;
    if (append(&listing, &entry)) goto memory_error;
    while ((item = smb2_readdir(context, directory))) {
        if (!item->name || !strcmp(item->name, ".") || !strcmp(item->name, "..") ||
            (!hidden && item->name[0] == '.') ||
            (item->st.smb2_type != SMB2_TYPE_FILE && item->st.smb2_type != SMB2_TYPE_DIRECTORY))
            continue;
        char ignored[128];
        if (nav_smb_url_child(&target, item->name, &child, ignored, sizeof ignored)) continue;
        memset(&entry, 0, sizeof entry);
        snprintf(entry.name, sizeof entry.name, "%s", item->name);
        snprintf(entry.resource_id, sizeof entry.resource_id, "%s", child.url);
        entry_stat(&entry, &item->st);
        if (append(&listing, &entry)) goto memory_error;
    }
    nav_listing_free(out);
    *out = listing;
    memset(&listing, 0, sizeof listing);
    result = 0;
    goto finish;
memory_error:
    snprintf(error, error_size, "out of memory receiving SMB listing");
finish:
    if (directory) smb2_closedir(context, directory);
    session_close(context);
    nav_listing_free(&listing);
    return result;
}

static int smb_stat(NavProvider *provider, const char *resource, NavEntry *entry,
                     char *error, size_t error_size)
{
    SmbProvider *smb = provider->context;
    NavSmbUrl target;
    struct smb2_stat_64 st;
    if (target_url(smb, resource, &target, error, error_size)) return -1;
    struct smb2_context *context = session_open(smb, error, error_size);
    if (!context) return -1;
    int result = checked_stat(context, &target, &st, error, error_size);
    if (!result) {
        memset(entry, 0, sizeof *entry);
        snprintf(entry->resource_id, sizeof entry->resource_id, "%s", target.url);
        const char *name = strrchr(target.path, '/') + 1;
        snprintf(entry->name, sizeof entry->name, "%s", *name ? name : target.share);
        entry_stat(entry, &st);
    }
    session_close(context);
    return result;
}

static int smb_open_read(NavProvider *provider, const char *resource, void **handle,
                          char *error, size_t error_size)
{
    SmbProvider *smb = provider->context;
    NavSmbUrl target;
    struct smb2_stat_64 st;
    SmbRead *read;
    *handle = NULL;
    if (target_url(smb, resource, &target, error, error_size)) return -1;
    read = calloc(1, sizeof *read);
    if (!read) { snprintf(error, error_size, "out of memory opening SMB file"); return -1; }
    read->context = session_open(smb, error, error_size);
    if (!read->context) goto fail;
    if (checked_stat(read->context, &target, &st, error, error_size)) goto fail;
    if (st.smb2_type != SMB2_TYPE_FILE) {
        snprintf(error, error_size, "SMB source is not a regular file");
        goto fail;
    }
    read->file = smb2_open(read->context, target.path + 1, O_RDONLY);
    if (!read->file) { smb_error(read->context, 0, "open", error, error_size); goto fail; }
    int result = smb2_fstat(read->context, read->file, &st);
    if (result) { smb_error(read->context, result, "stat", error, error_size); goto fail; }
    if (st.smb2_type != SMB2_TYPE_FILE) {
        snprintf(error, error_size, "SMB source is not a regular file");
        goto fail;
    }
    read->next = smb->reads;
    smb->reads = read;
    *handle = read;
    return 0;
fail:
    if (read->file) smb2_close(read->context, read->file);
    session_close(read->context);
    free(read);
    return -1;
}

static int smb_read(NavProvider *provider, void *handle, void *buffer, size_t capacity,
                     size_t *got, char *error, size_t error_size)
{
    SmbRead *read = handle;
    (void)provider;
    *got = 0;
    if (read->failed) { snprintf(error, error_size, "SMB read has failed"); return -1; }
    if (!capacity) return 0;
    uint32_t limit = smb2_get_max_read_size(read->context);
    if (!limit || limit > 65536) limit = 65536;
    if (capacity < limit) limit = (uint32_t)capacity;
    int result = smb2_read(read->context, read->file, buffer, limit);
    if (result < 0) {
        read->failed = true;
        return smb_error(read->context, result, "read", error, error_size);
    }
    *got = (size_t)result;
    return 0;
}

static int smb_close(NavProvider *provider, void *handle, char *error, size_t error_size)
{
    SmbProvider *smb = provider->context;
    SmbRead *read = handle, **link = &smb->reads;
    if (!read) return 0;
    while (*link && *link != read) link = &(*link)->next;
    if (!*link) { snprintf(error, error_size, "invalid SMB read handle"); return -1; }
    *link = read->next;
    int result = smb2_close(read->context, read->file);
    if (result) smb_error(read->context, result, "close", error, error_size);
    session_close(read->context);
    free(read);
    return result ? -1 : 0;
}

static void smb_destroy(NavProvider *provider)
{
    SmbProvider *smb = provider->context;
    char ignored[128];
    while (smb->reads) smb_close(provider, smb->reads, ignored, sizeof ignored);
    free(smb);
}

NavProvider *nav_smb_provider_create(const NavRepository *repository,
                                     NavCredentialStore *credentials,
                                     char *error, size_t error_size)
{
    SmbProvider *smb;
    NavSmbUrl root;
    if (!repository || !repository->name[0]) {
        snprintf(error, error_size, "SMB repository name is missing");
        return NULL;
    }
    if (nav_smb_url_parse(repository->url, &root, error, error_size)) return NULL;
    smb = calloc(1, sizeof *smb);
    if (!smb) { snprintf(error, error_size, "out of memory creating SMB provider"); return NULL; }
    smb->root = root;
    smb->credentials = credentials;
    snprintf(smb->name, sizeof smb->name, "%s", repository->name);
    snprintf(smb->credential_name, sizeof smb->credential_name, "%s", repository->credential);
    smb->provider = (NavProvider){
        .scheme = "smb", .display_name = smb->name, .context = smb,
        .capabilities = NAV_CAP_LIST | NAV_CAP_READ | NAV_CAP_STAT,
        .destroy = smb_destroy, .location = smb_location,
        .location_child = smb_child, .location_parent = smb_parent,
        .list = smb_list, .stat = smb_stat, .open_read = smb_open_read,
        .read = smb_read, .close = smb_close,
    };
    return &smb->provider;
}
