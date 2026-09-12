#include "fake_provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int append(NavListing *listing, const char *name, const char *id, unsigned flags)
{
    NavEntry *grown = realloc(listing->items, (listing->count + 1) * sizeof *grown);
    if (!grown) return -1;
    listing->items = grown;
    listing->capacity = listing->count + 1;
    NavEntry *entry = &listing->items[listing->count++];
    memset(entry, 0, sizeof *entry);
    snprintf(entry->name, sizeof entry->name, "%s", name);
    snprintf(entry->resource_id, sizeof entry->resource_id, "%s", id);
    entry->flags = flags;
    return 0;
}
static int location(NavProvider *provider, const char *id, NavLocation *out, char *error, size_t size)
{
    (void)error; (void)size; memset(out, 0, sizeof *out); out->provider = provider;
    snprintf(out->resource_id, sizeof out->resource_id, "%s", id);
    snprintf(out->display_path, sizeof out->display_path, "%s", !strcmp(id, "root:main") ? "Synthetic" : "Synthetic/alpha");
    return 0;
}
static int parent(NavProvider *provider, const NavLocation *from, NavLocation *out, char *error, size_t size)
{ return location(provider, !strcmp(from->resource_id, "root:main") ? "root:main" : "root:main", out, error, size); }
static int open_read(NavProvider *provider, const char *id, void **handle, char *error, size_t size)
{
    size_t *position; (void)provider; (void)id; (void)error; (void)size;
    position = calloc(1, sizeof *position); if (!position) return -1; *handle = position; return 0;
}
static int read_data(NavProvider *provider, void *handle, void *buffer, size_t capacity, size_t *got, char *error, size_t size)
{
    static const char text[] = "synthetic line one\nsynthetic line two\n"; size_t *position = handle;
    size_t remaining = sizeof text - 1 - *position; (void)provider; (void)error; (void)size;
    *got = remaining < capacity ? remaining : capacity; memcpy(buffer, text + *position, *got); *position += *got; return 0;
}
static int close_read(NavProvider *provider, void *handle, char *error, size_t size)
{ (void)provider; (void)error; (void)size; free(handle); return 0; }
static int list(NavProvider *provider, const char *id, bool hidden, NavListing *out, char *error, size_t size)
{
    (void)provider; (void)hidden; free(out->items); memset(out, 0, sizeof *out);
    if (!strcmp(id, "folder:alpha")) {
        if (append(out, "..", "root:main", NAV_ENTRY_DIR | NAV_ENTRY_PARENT) || append(out, "inside", "object:inside", 0)) goto memory;
        return 0;
    }
    if (strcmp(id, "root:main")) { snprintf(error, size, "unknown synthetic location"); return -1; }
    if (append(out, "..", "root:main", NAV_ENTRY_DIR | NAV_ENTRY_PARENT) || append(out, "alpha", "folder:alpha", NAV_ENTRY_DIR)) goto memory;
    for (int index = 0; index < 75; index++) {
        char name[32], opaque[32]; snprintf(name, sizeof name, "file%03d", index); snprintf(opaque, sizeof opaque, "object:%03d", index);
        if (append(out, name, opaque, 0)) goto memory;
        out->items[out->count - 1].size = (uint64_t)index + 10;
        out->items[out->count - 1].modified = (time_t)1700000000;
        out->items[out->count - 1].flags |= NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN;
    }
    return 0;
memory: snprintf(error, size, "out of memory"); free(out->items); memset(out, 0, sizeof *out); return -1;
}
NavProvider *nav_test_fake_provider(void)
{
    static NavProvider provider = {.scheme="synthetic", .capabilities=NAV_CAP_LIST|NAV_CAP_READ,
        .location=location, .location_parent=parent, .list=list,
        .open_read=open_read, .read=read_data, .close=close_read};
    return &provider;
}
