#include "nav.h"
#include <dirent.h>
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
static int local_list(NavProvider *p, const char *path, bool hidden, NavListing *out, char *err, size_t en) {
    DIR *d; struct dirent *de; NavEntry e; struct stat st; (void)p; nav_listing_free(out); d=opendir(path); if(!d){snprintf(err,en,"%s: %s",path,strerror(errno));return -1;}
    memset(&e,0,sizeof e); snprintf(e.name,sizeof e.name,"../"); snprintf(e.path,sizeof e.path,"%s",path); e.flags=NAV_ENTRY_DIR|NAV_ENTRY_PARENT; append(out,&e);
    while((de=readdir(d))) { size_t name_len; if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")||(!hidden&&de->d_name[0]=='.'))continue; memset(&e,0,sizeof e); if(nav_path_join(path,de->d_name,e.path,sizeof e.path)||lstat(e.path,&st))continue; name_len=strnlen(de->d_name,sizeof e.name-2);memcpy(e.name,de->d_name,name_len);if(S_ISDIR(st.st_mode)){e.name[name_len++]='/';e.flags|=NAV_ENTRY_DIR;}e.name[name_len]=0;e.size=(uint64_t)st.st_size;e.modified=st.st_mtime;if(append(out,&e)){closedir(d);snprintf(err,en,"out of memory");return -1;} }
    closedir(d);qsort(out->items,out->count,sizeof *out->items,cmp_entry);return 0;
}
static int local_move(NavProvider*p,const char*s,const char*d,char*e,size_t n){(void)p;if(rename(s,d)){snprintf(e,n,"%s",strerror(errno));return -1;}return 0;}
static int local_remove(NavProvider*p,const char*s,char*e,size_t n){(void)p;if(remove(s)){snprintf(e,n,"%s",strerror(errno));return -1;}return 0;}
static int local_mkdir(NavProvider*p,const char*s,char*e,size_t n){(void)p;if(mkdir(s,0777)){snprintf(e,n,"%s",strerror(errno));return -1;}return 0;}
static int local_stat(NavProvider*p,const char*path,NavEntry*out,char*e,size_t n){struct stat st;(void)p;if(lstat(path,&st)){snprintf(e,n,"%s: %s",path,strerror(errno));return -1;}memset(out,0,sizeof *out);snprintf(out->path,sizeof out->path,"%s",path);snprintf(out->name,sizeof out->name,"%s",nav_path_basename(path));out->size=(uint64_t)st.st_size;out->modified=st.st_mtime;if(S_ISDIR(st.st_mode))out->flags=NAV_ENTRY_DIR;return 0;}
static int local_open_read(NavProvider*p,const char*path,void**handle,char*e,size_t n){FILE*f;(void)p;f=fopen(path,"rb");if(!f){snprintf(e,n,"cannot open source: %s",strerror(errno));return -1;}*handle=f;return 0;}
static int local_open_write(NavProvider*p,const char*path,bool overwrite,void**handle,char*e,size_t n){int flags=O_WRONLY|O_CREAT|(overwrite?O_TRUNC:O_EXCL);int fd;(void)p;fd=open(path,flags,0666);if(fd<0){snprintf(e,n,"cannot create destination: %s",strerror(errno));return -1;}*handle=fdopen(fd,"wb");if(!*handle){snprintf(e,n,"cannot create destination: %s",strerror(errno));close(fd);return -1;}return 0;}
static int local_read(NavProvider*p,void*h,void*b,size_t cap,size_t*got,char*e,size_t n){(void)p;*got=fread(b,1,cap,(FILE*)h);if(*got==0&&ferror((FILE*)h)){snprintf(e,n,"read failure: %s",strerror(errno));return -1;}return 0;}
static int local_write(NavProvider*p,void*h,const void*b,size_t len,char*e,size_t n){(void)p;if(fwrite(b,1,len,(FILE*)h)!=len){snprintf(e,n,"write failure: %s",strerror(errno));return -1;}return 0;}
static int local_close(NavProvider*p,void*h,char*e,size_t n){(void)p;if(fclose((FILE*)h)){snprintf(e,n,"close failure: %s",strerror(errno));return -1;}return 0;}
NavProvider *nav_local_provider(void) { static NavProvider p={"local",NAV_CAP_LIST|NAV_CAP_READ|NAV_CAP_WRITE|NAV_CAP_DELETE|NAV_CAP_MKDIR,local_list,local_move,local_remove,local_mkdir,local_stat,local_open_read,local_open_write,local_read,local_write,local_close};return &p; }
