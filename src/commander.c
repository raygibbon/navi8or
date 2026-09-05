#include "nav.h"
#include <stdio.h>
#include <string.h>

void nav_history_push(NavHistory *h, const char *path) { if (h->current >= 0 && h->current < h->count && !strcmp(h->paths[h->current],path)) return; if(h->current+1<h->count)h->count=h->current+1; if(h->count==64){memmove(h->paths,h->paths+1,63*sizeof h->paths[0]);--h->count;} snprintf(h->paths[h->count],NAV_PATH_MAX,"%s",path);h->current=h->count++; }
const char *nav_history_back(NavHistory *h) { return h->current>0?h->paths[--h->current]:NULL; }
const char *nav_history_forward(NavHistory *h) { return h->current+1<h->count?h->paths[++h->current]:NULL; }
int nav_pane_load(NavPane *p,const char *path,bool hidden,bool history,char *err,size_t en) { char normalized[NAV_PATH_MAX]; if(nav_path_normalize(path,normalized,sizeof normalized)){snprintf(err,en,"invalid path");return -1;}if(p->provider->list(p->provider,normalized,hidden,&p->listing,err,en))return -1;snprintf(p->path,sizeof p->path,"%s",normalized);p->selected=0;p->offset=0;if(history)nav_history_push(&p->history,p->path);return 0; }
static bool shown(const NavPane *p,const NavEntry *e){return !p->filter[0]||strstr(e->name,p->filter)!=NULL;}
int nav_pane_visible_count(const NavPane *p){int n=0;for(size_t i=0;i<p->listing.count;i++)if(shown(p,&p->listing.items[i]))n++;return n;}
const NavEntry *nav_pane_selected(const NavPane *p){int n=0;for(size_t i=0;i<p->listing.count;i++)if(shown(p,&p->listing.items[i])&&n++==p->selected)return &p->listing.items[i];return NULL;}
