/* Indexed, read-only local source. The viewer depends only on NavViewSource. */
#include "nav_view.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef struct { off_t offset; size_t length; } LocalLine;
typedef struct {
    FILE *file;
    LocalLine *lines;
    size_t count,capacity;
    char *cache;
    size_t cache_capacity;
} LocalSource;

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
