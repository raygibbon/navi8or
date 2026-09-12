/* Indexed, read-only local source. The viewer depends only on NavViewSource. */
#include "nav_view.h"
#include "nav.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct { off_t offset; size_t length; } LocalLine;
typedef struct {
    FILE *file;
    LocalLine *lines;
    size_t count,capacity;
    char *cache;
    size_t cache_capacity;
} LocalSource;

#define REMOTE_VIEW_BLOCK (64u * 1024u)
#define REMOTE_CACHE_BLOCKS 16u
#define REMOTE_LINE_LIMIT (8u * 1024u * 1024u)

typedef struct {
    uint64_t index, stamp;
    size_t length;
    bool valid;
    unsigned char data[REMOTE_VIEW_BLOCK];
} RemoteBlock;
typedef struct {
    NavProvider *provider;
    char resource_id[NAV_PATH_MAX];
    RemoteBlock blocks[REMOTE_CACHE_BLOCKS];
    uint64_t stamp, total;
    bool total_known;
    char *line_cache;
    size_t line_cache_capacity;
    char error[256];
} RemoteSource;

static size_t local_count(NavViewSource *source){return ((LocalSource *)source->implementation)->count;}
static size_t local_length(NavViewSource *source,size_t line){
    LocalSource *local=source->implementation;
    return line<local->count?local->lines[line].length:0;
}
static const char *local_line(NavViewSource *source,size_t line,size_t *length){
    LocalSource *local=source->implementation;
    size_t wanted;
    if(line>=local->count)return NULL;
    wanted=local->lines[line].length;
    if(wanted+1>local->cache_capacity){
        char *grown=realloc(local->cache,wanted+1);
        if(!grown)return NULL;
        local->cache=grown;local->cache_capacity=wanted+1;
    }
    if(fseeko(local->file,local->lines[line].offset,SEEK_SET)!=0||
       fread(local->cache,1,wanted,local->file)!=wanted)return NULL;
    local->cache[wanted]=0;
    if(length)*length=wanted;
    return local->cache;
}
static void local_close(NavViewSource *source){
    if(source){LocalSource *local=source->implementation;if(local){if(local->file)fclose(local->file);free(local->lines);free(local->cache);free(local);}free(source);}
}

static int append_line(LocalSource *local,off_t offset,size_t length){
    if(local->count==local->capacity){
        size_t capacity=local->capacity?local->capacity*2:1024;
        LocalLine *grown=realloc(local->lines,capacity*sizeof *grown);
        if(!grown)return -1;
        local->lines=grown;local->capacity=capacity;
    }
    local->lines[local->count++]=(LocalLine){offset,length};
    return 0;
}

static RemoteBlock *remote_block(RemoteSource *remote, uint64_t index)
{
    RemoteBlock *chosen = NULL;
    size_t got = 0;
    bool eof = false, known = false;
    uint64_t total = 0, offset;
    if (index > UINT64_MAX / REMOTE_VIEW_BLOCK) return NULL;
    offset = index * REMOTE_VIEW_BLOCK;
    if (remote->total_known && offset >= remote->total) return NULL;
    for (size_t i = 0; i < REMOTE_CACHE_BLOCKS; i++) {
        if (remote->blocks[i].valid && remote->blocks[i].index == index) {
            remote->blocks[i].stamp = ++remote->stamp;
            return &remote->blocks[i];
        }
        if (!chosen || !remote->blocks[i].valid ||
            (chosen->valid && remote->blocks[i].stamp < chosen->stamp))
            chosen = &remote->blocks[i];
    }
    if (!chosen || remote->provider->read_at(
            remote->provider, remote->resource_id, offset, chosen->data,
            sizeof chosen->data, &got, &eof, &total, &known, remote->error,
            sizeof remote->error)) return NULL;
    if (got > sizeof chosen->data || (!got && !eof)) {
        snprintf(remote->error, sizeof remote->error,
                 "invalid bounded remote read");
        return NULL;
    }
    if (known) {
        if (remote->total_known && remote->total != total) {
            snprintf(remote->error, sizeof remote->error,
                     "remote resource changed while viewing");
            return NULL;
        }
        remote->total = total;
        remote->total_known = true;
    } else if (eof) {
        remote->total = offset + got;
        remote->total_known = true;
    }
    chosen->index = index;
    chosen->length = got;
    chosen->stamp = ++remote->stamp;
    chosen->valid = true;
    return chosen;
}

/* Returns one for a byte, zero at known EOF, and minus one on read failure. */
static int remote_byte(RemoteSource *remote, uint64_t offset,
                       unsigned char *value)
{
    RemoteBlock *block;
    size_t within;
    if (remote->total_known && offset >= remote->total) return 0;
    block = remote_block(remote, offset / REMOTE_VIEW_BLOCK);
    if (!block) return remote->error[0] ? -1 : 0;
    within = (size_t)(offset % REMOTE_VIEW_BLOCK);
    if (within >= block->length) return 0;
    *value = block->data[within];
    return 1;
}

static int remote_next(RemoteSource *remote, uint64_t offset, uint64_t *next)
{
    unsigned char ch;
    int result;
    while ((result = remote_byte(remote, offset, &ch)) > 0) {
        if (ch == '\n') {
            if (offset == UINT64_MAX) return 0;
            *next = offset + 1;
            if (remote->total_known && *next >= remote->total) return 0;
            return 1;
        }
        if (offset == UINT64_MAX) return 0;
        offset++;
    }
    return result;
}

static int remote_previous(RemoteSource *remote, uint64_t offset,
                           uint64_t *previous)
{
    unsigned char ch;
    int result;
    if (!offset) return 0;
    offset--;
    result = remote_byte(remote, offset, &ch);
    if (result <= 0) return result;
    if (ch == '\n' && offset) offset--;
    for (;;) {
        result = remote_byte(remote, offset, &ch);
        if (result <= 0) return result;
        if (ch == '\n') { *previous = offset + 1; return 1; }
        if (!offset) { *previous = 0; return 1; }
        offset--;
    }
}

static int remote_cursor_top(NavViewSource *source, NavViewCursor *cursor)
{
    (void)source;
    *cursor = (NavViewCursor){.offset = 0, .ordinal = 0,
                             .ordinal_known = true};
    return 0;
}

static int remote_cursor_bottom(NavViewSource *source, NavViewCursor *cursor)
{
    RemoteSource *remote = source->implementation;
    uint64_t previous;
    unsigned char ignored;
    if (!remote->total_known && remote_byte(remote, 0, &ignored) < 0) return -1;
    if (!remote->total_known) {
        snprintf(remote->error, sizeof remote->error,
                 "remote size is unknown; End is unavailable");
        return -1;
    }
    if (!remote->total) return remote_cursor_top(source, cursor);
    if (remote_previous(remote, remote->total, &previous) <= 0) previous = 0;
    *cursor = (NavViewCursor){.offset = previous};
    return 0;
}

static int remote_cursor_move(NavViewSource *source, NavViewCursor *cursor,
                              long delta, long *moved)
{
    RemoteSource *remote = source->implementation;
    long amount = 0;
    while (delta > 0) {
        uint64_t next;
        int result = remote_next(remote, cursor->offset, &next);
        if (result < 0) return -1;
        if (!result) break;
        cursor->offset = next;
        if (cursor->ordinal_known) cursor->ordinal++;
        delta--; amount++;
    }
    while (delta < 0) {
        uint64_t previous;
        int result = remote_previous(remote, cursor->offset, &previous);
        if (result < 0) return -1;
        if (!result) break;
        cursor->offset = previous;
        if (cursor->ordinal_known && cursor->ordinal) cursor->ordinal--;
        else cursor->ordinal_known = false;
        delta++; amount--;
    }
    if (moved) *moved = amount;
    return 0;
}

static const char *remote_cursor_line(NavViewSource *source,
                                      const NavViewCursor *cursor,
                                      size_t *length)
{
    RemoteSource *remote = source->implementation;
    uint64_t offset = cursor->offset;
    size_t used = 0;
    unsigned char ch;
    int result;
    while ((result = remote_byte(remote, offset, &ch)) > 0 && ch != '\n') {
        if (used >= REMOTE_LINE_LIMIT) {
            snprintf(remote->error, sizeof remote->error,
                     "Line exceeds remote Viewer limit (8 MiB)");
            return NULL;
        }
        if (used + 1 >= remote->line_cache_capacity) {
            size_t capacity = remote->line_cache_capacity ?
                              remote->line_cache_capacity * 2 : 4096;
            char *grown;
            while (capacity <= used + 1) {
                if (capacity > SIZE_MAX / 2) {
                    snprintf(remote->error, sizeof remote->error,
                             "remote line is too long");
                    return NULL;
                }
                capacity *= 2;
            }
            grown = realloc(remote->line_cache, capacity);
            if (!grown) {
                snprintf(remote->error, sizeof remote->error,
                         "out of memory reading remote line");
                return NULL;
            }
            remote->line_cache = grown;
            remote->line_cache_capacity = capacity;
        }
        remote->line_cache[used++] = (char)ch;
        if (offset == UINT64_MAX) break;
        offset++;
    }
    if (result < 0) return NULL;
    if (used && remote->line_cache[used - 1] == '\r') used--;
    if (!remote->line_cache) {
        remote->line_cache = malloc(1);
        if (!remote->line_cache) return NULL;
        remote->line_cache_capacity = 1;
    }
    remote->line_cache[used] = 0;
    if (length) *length = used;
    return remote->line_cache;
}

static const char *remote_last_error(NavViewSource *source)
{
    return ((RemoteSource *)source->implementation)->error;
}

static bool remote_cursor_find(NavViewSource *source,
                               const NavViewCursor *start, const char *needle,
                               int direction, NavViewCursor *found,
                               size_t *column, bool *wrapped)
{
    NavViewCursor cursor = *start;
    bool did_wrap = false;
    for (int pass = 0; pass < 2; pass++) {
        for (;;) {
            long moved = 0;
            if (remote_cursor_move(source, &cursor, direction > 0 ? 1 : -1,
                                   &moved) || !moved) break;
            if (did_wrap && ((direction > 0 && cursor.offset >= start->offset) ||
                             (direction < 0 && cursor.offset <= start->offset)))
                break;
            size_t length = 0;
            const char *line = remote_cursor_line(source, &cursor, &length);
            const char *match = line ? strstr(line, needle) : NULL;
            (void)length;
            if (match) {
                *found = cursor;
                if (column) *column = (size_t)(match - line);
                if (wrapped) *wrapped = did_wrap;
                return true;
            }
            if (did_wrap && cursor.offset == start->offset) break;
        }
        if (direction > 0)
            remote_cursor_top(source, &cursor);
        else if (remote_cursor_bottom(source, &cursor)) break;
        did_wrap = true;
        if (cursor.offset == start->offset) break;
        {
            size_t length = 0;
            const char *line = remote_cursor_line(source, &cursor, &length);
            const char *match = line ? strstr(line, needle) : NULL;
            (void)length;
            if (match) {
                *found = cursor;
                if (column) *column = (size_t)(match - line);
                if (wrapped) *wrapped = true;
                return true;
            }
        }
    }
    return false;
}

static bool remote_position(NavViewSource *source, const NavViewCursor *cursor,
                            uint64_t *offset, uint64_t *total, bool *known)
{
    RemoteSource *remote = source->implementation;
    if (offset) *offset = cursor->offset;
    if (total) *total = remote->total;
    if (known) *known = remote->total_known;
    return true;
}

static void remote_close(NavViewSource *source)
{
    if (source) {
        RemoteSource *remote = source->implementation;
        if (remote) {
            free(remote->line_cache);
            free(remote);
        }
        free(source);
    }
}

static NavViewSource *open_remote_source(NavProvider *provider,
                                         const char *resource_id, bool *binary,
                                         char *error, size_t error_size)
{
    NavViewSource *source = calloc(1, sizeof *source);
    RemoteSource *remote = calloc(1, sizeof *remote);
    size_t probe = 0, controls = 0;
    RemoteBlock *first;
    if (!source || !remote) goto memory_error;
    remote->provider = provider;
    snprintf(remote->resource_id, sizeof remote->resource_id, "%s", resource_id);
    first = remote_block(remote, 0);
    if (!first && remote->error[0]) {
        snprintf(error, error_size, "%s", remote->error[0] ? remote->error :
                 "unable to read remote text");
        goto fail;
    }
    probe = first && first->length < 4096 ? first->length : first ? 4096 : 0;
    for (size_t index = 0; index < probe; index++) {
        unsigned char ch = first->data[index];
        if (ch == 0) { if (binary) *binary = true; goto fail; }
        if (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t' && ch != '\f')
            controls++;
    }
    if (probe && controls * 20 > probe) {
        if (binary) *binary = true;
        goto fail;
    }
    source->cursor_top = remote_cursor_top;
    source->cursor_bottom = remote_cursor_bottom;
    source->cursor_move = remote_cursor_move;
    source->cursor_line = remote_cursor_line;
    source->cursor_find = remote_cursor_find;
    source->position = remote_position;
    source->last_error = remote_last_error;
    source->close = remote_close;
    source->implementation = remote;
    return source;
memory_error:
    snprintf(error, error_size, "out of memory opening remote viewer");
fail:
    if (remote) { free(remote->line_cache); free(remote); }
    free(source);
    return NULL;
}

NavViewSource *nav_view_source_open_local(const char *path,bool *binary,
                                          char *error,size_t error_size){
    NavViewSource *source=calloc(1,sizeof *source);
    LocalSource *local=calloc(1,sizeof *local);
    char *line=NULL;size_t line_capacity=0,probed=0,controls=0;ssize_t length;
    bool is_binary=false;
    if(binary)*binary=false;
    if(!source||!local)goto memory_error;
    local->file=fopen(path,"rb");
    if(!local->file){snprintf(error,error_size,"cannot open: %s",strerror(errno));goto fail;}
    while(1){
        off_t offset=ftello(local->file);
        length=getline(&line,&line_capacity,local->file);
        if(length<0)break;
        for(ssize_t i=0;i<length&&probed<4096;i++,probed++){
            unsigned char ch=(unsigned char)line[i];
            if(ch==0)is_binary=true;
            else if(ch<32&&ch!='\n'&&ch!='\r'&&ch!='\t'&&ch!='\f')controls++;
        }
        size_t logical=(size_t)length;
        if(logical&&line[logical-1]=='\n')logical--;
        if(logical&&line[logical-1]=='\r')logical--;
        if(append_line(local,offset,logical)<0)goto memory_error;
        if(is_binary||(probed>=4096&&controls*20>probed)){is_binary=true;break;}
    }
    if(ferror(local->file)){snprintf(error,error_size,"cannot read: %s",strerror(errno));goto fail;}
    if(probed&&controls*20>probed)is_binary=true;
    free(line);
    if(is_binary){
        if(binary)*binary=true;
        fclose(local->file);free(local->lines);free(local->cache);free(local);free(source);
        return NULL;
    }
    source->line_count=local_count;source->line=local_line;
    source->line_length=local_length;source->close=local_close;source->implementation=local;
    return source;
memory_error:
    snprintf(error,error_size,"out of memory");
fail:
    free(line);
    if(local){if(local->file)fclose(local->file);free(local->lines);free(local->cache);free(local);}
    free(source);
    return NULL;
}

NavViewSource *nav_view_source_open_provider(NavProvider *provider, const char *resource_id,
                                             bool *binary, char *error, size_t error_size)
{
    char temporary[] = "/tmp/nav-view-source-XXXXXX";
    unsigned char buffer[65536];
    void *handle = NULL;
    int descriptor;
    FILE *file;
    if (binary) *binary = false;
    if (!provider || !(provider->capabilities & NAV_CAP_READ) || !provider->open_read ||
        !provider->read || !provider->close) { snprintf(error, error_size, "provider cannot read this resource"); return NULL; }
    if ((provider->capabilities & NAV_CAP_RANDOM_READ) && provider->read_at)
        return open_remote_source(provider, resource_id, binary, error, error_size);
    if (provider->open_read(provider, resource_id, &handle, error, error_size)) return NULL;
    descriptor = mkstemp(temporary);
    if (descriptor < 0) { provider->close(provider, handle, error, error_size); snprintf(error, error_size, "cannot create viewer cache"); return NULL; }
    file = fdopen(descriptor, "wb");
    if (!file) { close(descriptor); unlink(temporary); provider->close(provider, handle, error, error_size); snprintf(error, error_size, "cannot create viewer cache"); return NULL; }
    for (;;) {
        size_t got = 0;
        if (provider->read(provider, handle, buffer, sizeof buffer, &got, error, error_size) ||
            (got && fwrite(buffer, 1, got, file) != got)) { fclose(file); unlink(temporary); provider->close(provider, handle, error, error_size); if (!error[0]) snprintf(error, error_size, "cannot cache viewer resource"); return NULL; }
        if (!got) break;
    }
    if (provider->close(provider, handle, error, error_size) || fclose(file)) { unlink(temporary); if (!error[0]) snprintf(error, error_size, "cannot close viewer resource"); return NULL; }
    NavViewSource *source = nav_view_source_open_local(temporary, binary, error, error_size);
    unlink(temporary);
    return source;
}
