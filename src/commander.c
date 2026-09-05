#include "nav.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

void nav_history_push(NavHistory *h, const char *path) { if (h->current >= 0 && h->current < h->count && !strcmp(h->paths[h->current],path)) return; if(h->current+1<h->count)h->count=h->current+1; if(h->count==64){memmove(h->paths,h->paths+1,63*sizeof h->paths[0]);--h->count;} snprintf(h->paths[h->count],NAV_PATH_MAX,"%s",path);h->current=h->count++; }
const char *nav_history_back(NavHistory *h) { return h->current>0?h->paths[--h->current]:NULL; }
const char *nav_history_forward(NavHistory *h) { return h->current+1<h->count?h->paths[++h->current]:NULL; }
int nav_pane_load(NavPane *p,const char *path,bool hidden,bool history,char *err,size_t en) { char normalized[NAV_PATH_MAX]; if(nav_path_normalize(path,normalized,sizeof normalized)){snprintf(err,en,"invalid path");return -1;}if(p->provider->list(p->provider,normalized,hidden,&p->listing,err,en))return -1;snprintf(p->path,sizeof p->path,"%s",normalized);p->selected=0;p->offset=0;if(history)nav_history_push(&p->history,p->path);return 0; }
static bool shown(const NavPane *p,const NavEntry *e){return !p->filter[0]||strstr(e->name,p->filter)!=NULL;}
int nav_pane_visible_count(const NavPane *p){int n=0;for(size_t i=0;i<p->listing.count;i++)if(shown(p,&p->listing.items[i]))n++;return n;}
void nav_pane_clamp_selection(NavPane *p){int n=nav_pane_visible_count(p);if(n<=0){p->selected=0;p->offset=0;return;}if(p->selected<0)p->selected=0;if(p->selected>=n)p->selected=n-1;if(p->offset<0)p->offset=0;if(p->offset>=n)p->offset=n-1;}
void nav_pane_ensure_visible(NavPane *p,int rows){nav_pane_clamp_selection(p);if(rows<1||nav_pane_visible_count(p)==0)return;if(p->selected<p->offset)p->offset=p->selected;else if(p->selected>=p->offset+rows)p->offset=p->selected-rows+1;}
const NavEntry *nav_pane_selected(const NavPane *p){int n=0;for(size_t i=0;i<p->listing.count;i++)if(shown(p,&p->listing.items[i])&&n++==p->selected)return &p->listing.items[i];return NULL;}
static int compare(const NavEntry*a,const NavEntry*b,NavSortMode mode){if(a->flags&NAV_ENTRY_PARENT)return -1;if(b->flags&NAV_ENTRY_PARENT)return 1;if(!!(a->flags&NAV_ENTRY_DIR)!=!!(b->flags&NAV_ENTRY_DIR))return(a->flags&NAV_ENTRY_DIR)?-1:1;if(mode==NAV_SORT_SIZE&&a->size!=b->size)return a->size<b->size?-1:1;if(mode==NAV_SORT_DATE&&a->modified!=b->modified)return a->modified>b->modified?-1:1;return strcasecmp(a->name,b->name);}
void nav_pane_sort(NavPane*p,NavSortMode mode){char selected[NAV_PATH_MAX]={0};const NavEntry*current=nav_pane_selected(p);if(current)snprintf(selected,sizeof selected,"%s",current->path);for(size_t i=1;i<p->listing.count;i++){NavEntry item=p->listing.items[i];size_t j=i;while(j>0&&compare(&item,&p->listing.items[j-1],mode)<0){p->listing.items[j]=p->listing.items[j-1];j--;}p->listing.items[j]=item;}p->sort_mode=mode;p->selected=0;if(selected[0]){int visible=0;for(size_t i=0;i<p->listing.count;i++)if(shown(p,&p->listing.items[i])){if(!strcmp(selected,p->listing.items[i].path)){p->selected=visible;break;}visible++;}}nav_pane_clamp_selection(p);}
