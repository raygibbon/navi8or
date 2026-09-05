#include "nav.h"
#include <dirent.h>
#include <errno.h>
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
static int copy_file(const char *s,const char *d,char *e,size_t n) { FILE *in=fopen(s,"rb"),*out; char b[65536];size_t r;if(!in){snprintf(e,n,"%s",strerror(errno));return -1;}out=fopen(d,"wb");if(!out){snprintf(e,n,"%s",strerror(errno));fclose(in);return -1;}while((r=fread(b,1,sizeof b,in))&&fwrite(b,1,r,out)!=r){snprintf(e,n,"%s",strerror(errno));fclose(in);fclose(out);return -1;}fclose(in);if(fclose(out)){snprintf(e,n,"%s",strerror(errno));return -1;}return 0; }
static int local_copy(NavProvider*p,const char*s,const char*d,char*e,size_t n){struct stat st;(void)p;if(lstat(s,&st)){snprintf(e,n,"%s",strerror(errno));return -1;}if(S_ISDIR(st.st_mode)){snprintf(e,n,"directory copy is not yet implemented");return -1;}return copy_file(s,d,e,n);}
static int local_move(NavProvider*p,const char*s,const char*d,char*e,size_t n){(void)p;if(rename(s,d)){snprintf(e,n,"%s",strerror(errno));return -1;}return 0;}
static int local_remove(NavProvider*p,const char*s,char*e,size_t n){(void)p;if(remove(s)){snprintf(e,n,"%s",strerror(errno));return -1;}return 0;}
static int local_mkdir(NavProvider*p,const char*s,char*e,size_t n){(void)p;if(mkdir(s,0777)){snprintf(e,n,"%s",strerror(errno));return -1;}return 0;}
NavProvider *nav_local_provider(void) { static NavProvider p={"local",NAV_CAP_LIST|NAV_CAP_READ|NAV_CAP_WRITE|NAV_CAP_DELETE|NAV_CAP_MKDIR,local_list,local_copy,local_move,local_remove,local_mkdir};return &p; }
