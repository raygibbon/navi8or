#include "nav.h"
#include "../platform/windows_internal.h"
#include <io.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int cmp_entry(const void *a, const void *b) { const NavEntry *x=a,*y=b; if (!!(y->flags&NAV_ENTRY_DIR)!=!!(x->flags&NAV_ENTRY_DIR)) return (y->flags&NAV_ENTRY_DIR)?1:-1; return strcasecmp(x->name,y->name); }
void nav_listing_free(NavListing *l) { free(l->items); memset(l, 0, sizeof *l); }
static int append(NavListing *l, const NavEntry *e) { if (l->count == l->capacity) { size_t nc=l->capacity?l->capacity*2:64; NavEntry *p=realloc(l->items,nc*sizeof *p); if(!p)return -1;l->items=p;l->capacity=nc;} l->items[l->count++]=*e;return 0; }
static int local_location(NavProvider *p, const char *input, NavLocation *out, char *err, size_t en) {
    char normalized[NAV_PATH_MAX];
    if (nav_path_normalize(input, normalized, sizeof normalized)) { snprintf(err, en, "invalid local path"); return -1; }
    memset(out, 0, sizeof *out); out->provider = p;
    snprintf(out->resource_id, sizeof out->resource_id, "%s", normalized);
    snprintf(out->display_path, sizeof out->display_path, "%s", normalized);
    return 0;
}
static int local_child(NavProvider *p, const NavLocation *parent, const char *name, NavLocation *out, char *err, size_t en) {
    char path[NAV_PATH_MAX];
    if (!parent || parent->provider != p || nav_path_join(parent->resource_id, name, path, sizeof path)) { snprintf(err, en, "invalid local child path"); return -1; }
    return local_location(p, path, out, err, en);
}
static int local_parent(NavProvider *p, const NavLocation *location, NavLocation *out, char *err, size_t en) {
    char path[NAV_PATH_MAX];
    if (!location || location->provider != p || nav_path_parent(location->resource_id, path, sizeof path)) { snprintf(err, en, "invalid local parent path"); return -1; }
    return local_location(p, path, out, err, en);
}
/* Adapted from TDX port.c's FindFirstFile/FindNextFile enumeration,
 * using wide APIs at Navi8or's UTF-8 boundary. */
static void metadata(const WIN32_FILE_ATTRIBUTE_DATA *data, NavEntry *entry)
{
    ULARGE_INTEGER ticks; ticks.HighPart = data->ftLastWriteTime.dwHighDateTime;
    ticks.LowPart = data->ftLastWriteTime.dwLowDateTime;
    entry->size = ((uint64_t)data->nFileSizeHigh << 32) | data->nFileSizeLow;
    entry->modified = (time_t)(ticks.QuadPart / 10000000ULL - 11644473600ULL);
    entry->flags = NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN;
    if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) entry->flags |= NAV_ENTRY_DIR;
}
static int local_stat(NavProvider *provider, const char *path, NavEntry *entry, char *error, size_t size)
{
    wchar_t *wide = nav_windows_wide(path); WIN32_FILE_ATTRIBUTE_DATA data; (void)provider;
    BOOL ok = wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &data);
    free(wide);
    if (!ok) { snprintf(error, size, "cannot inspect local file"); return -1; }
    memset(entry, 0, sizeof *entry); metadata(&data, entry);
    snprintf(entry->name, sizeof entry->name, "%s", nav_path_basename(path));
    snprintf(entry->resource_id, sizeof entry->resource_id, "%s", path); return 0;
}
static int local_list(NavProvider *provider, const char *path, bool hidden, NavListing *out, char *error, size_t size)
{
    char pattern[NAV_PATH_MAX]; WIN32_FIND_DATAW found; NavEntry entry;
    nav_listing_free(out);
    if (snprintf(pattern, sizeof pattern, "%s/*", path) >= (int)sizeof pattern) return -1;
    wchar_t *wide = nav_windows_wide(pattern);
    HANDLE search = wide ? FindFirstFileW(wide, &found) : INVALID_HANDLE_VALUE;
    free(wide);
    if (search == INVALID_HANDLE_VALUE) { snprintf(error, size, "cannot enumerate local directory"); return -1; }
    memset(&entry, 0, sizeof entry); strcpy(entry.name, "..");
    snprintf(entry.resource_id, sizeof entry.resource_id, "%s", path);
    entry.flags = NAV_ENTRY_DIR | NAV_ENTRY_PARENT;
    if (append(out, &entry)) goto fail;
    do {
        char name[NAV_NAME_MAX], child[NAV_PATH_MAX];
        if (nav_windows_utf8(found.cFileName, name, sizeof name)) continue;
        if (!strcmp(name, ".") || !strcmp(name, "..") ||
            (!hidden && (name[0] == '.' || (found.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)))) continue;
        if (nav_path_join(path, name, child, sizeof child) || local_stat(provider, child, &entry, error, size)) continue;
        if (append(out, &entry)) goto fail;
    } while (FindNextFileW(search, &found));
    DWORD status = GetLastError(); FindClose(search);
    if (status != ERROR_NO_MORE_FILES) { snprintf(error, size, "directory enumeration failed"); return -1; }
    qsort(out->items, out->count, sizeof *out->items, cmp_entry); return 0;
fail:
    FindClose(search); snprintf(error, size, "out of memory"); return -1;
}
static int local_move(NavProvider*p,const char*s,const char*d,char*e,size_t n){(void)p;if(nav_platform_replace(s,d)){snprintf(e,n,"%s",strerror(errno));return -1;}return 0;}
static int local_remove(NavProvider*p,const char*s,char*e,size_t n){(void)p;wchar_t *wide=nav_windows_wide(s);int result=wide?((GetFileAttributesW(wide)&FILE_ATTRIBUTE_DIRECTORY)?_wrmdir(wide):_wunlink(wide)):-1;free(wide);if(result){snprintf(e,n,"%s",strerror(errno));return -1;}return 0;}
static int local_mkdir(NavProvider*p,const char*s,char*e,size_t n){(void)p;if(nav_platform_mkdir(s,0777)){snprintf(e,n,"%s",strerror(errno));return -1;}return 0;}
static int local_open_read(NavProvider*p,const char*path,void**handle,char*e,size_t n){FILE*f;(void)p;f=nav_platform_fopen(path,"rb");if(!f){snprintf(e,n,"cannot open source: %s",strerror(errno));return -1;}*handle=f;return 0;}
static int local_open_write(NavProvider*p,const char*path,bool overwrite,uint64_t total,bool total_known,void**handle,char*e,size_t n){int flags=O_WRONLY|O_CREAT|(overwrite?O_TRUNC:O_EXCL);int fd;(void)p;(void)total;(void)total_known;wchar_t *wide=nav_windows_wide(path);fd=wide?_wopen(wide,flags|_O_BINARY,_S_IREAD|_S_IWRITE):-1;free(wide);if(fd<0){snprintf(e,n,"cannot create destination: %s",strerror(errno));return -1;}*handle=fdopen(fd,"wb");if(!*handle){snprintf(e,n,"cannot create destination: %s",strerror(errno));close(fd);return -1;}return 0;}
static int local_read(NavProvider*p,void*h,void*b,size_t cap,size_t*got,char*e,size_t n){(void)p;*got=fread(b,1,cap,(FILE*)h);if(*got==0&&ferror((FILE*)h)){snprintf(e,n,"read failure: %s",strerror(errno));return -1;}return 0;}
static int local_write(NavProvider*p,void*h,const void*b,size_t len,char*e,size_t n){(void)p;if(fwrite(b,1,len,(FILE*)h)!=len){snprintf(e,n,"write failure: %s",strerror(errno));return -1;}return 0;}
static int local_close(NavProvider*p,void*h,char*e,size_t n){(void)p;if(fclose((FILE*)h)){snprintf(e,n,"close failure: %s",strerror(errno));return -1;}return 0;}
NavProvider *nav_local_provider(void) { static NavProvider p={.scheme="local",.capabilities=NAV_CAP_LIST|NAV_CAP_READ|NAV_CAP_WRITE|NAV_CAP_DELETE|NAV_CAP_MKDIR|NAV_CAP_RENAME|NAV_CAP_STAT|NAV_CAP_EDIT_LOCAL,.location=local_location,.location_child=local_child,.location_parent=local_parent,.list=local_list,.rename_path=local_move,.remove=local_remove,.mkdir=local_mkdir,.stat=local_stat,.open_read=local_open_read,.open_write=local_open_write,.read=local_read,.write=local_write,.close=local_close};return &p; }
