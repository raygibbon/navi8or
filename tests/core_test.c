#include "nav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { uint64_t done,total; int calls; } Progress;
static void progress(uint64_t done,uint64_t total,void *data){Progress*p=data;p->done=done;p->total=total;p->calls++;}
int main(void){char root[]="/tmp/nav-test-XXXXXX",source[NAV_PATH_MAX],destination[NAV_PATH_MAX],small[NAV_PATH_MAX],error[256];unsigned char data[200000];NavProvider*provider=nav_local_provider();Progress state={0};FILE*file;struct stat st;NavPane pane={0};NavHistory history={.current=-1};
    assert(mkdtemp(root)!=NULL);assert(nav_path_join(root,"file 'with spaces'.bin",source,sizeof source)==0);assert(nav_path_join(root,"copy.bin",destination,sizeof destination)==0);
    for(size_t i=0;i<sizeof data;i++)data[i]=(unsigned char)(i&255);
    file=fopen(source,"wb");assert(file);assert(fwrite(data,1,sizeof data,file)==sizeof data);assert(fclose(file)==0);
    assert(nav_transfer_copy(provider,source,provider,destination,false,progress,&state,error,sizeof error)==0);assert(state.calls>1);assert(state.done==sizeof data&&state.total==sizeof data);assert(stat(destination,&st)==0&&(size_t)st.st_size==sizeof data);
    assert(nav_transfer_copy(provider,source,provider,destination,false,NULL,NULL,error,sizeof error)!=0);assert(nav_transfer_copy(provider,source,provider,source,true,NULL,NULL,error,sizeof error)!=0);
    assert(nav_path_join(root,"small.txt",small,sizeof small)==0);file=fopen(small,"wb");assert(file);assert(fwrite("x",1,1,file)==1);assert(fclose(file)==0);
    pane.provider=provider;pane.history.current=-1;assert(nav_pane_load(&pane,root,true,true,error,sizeof error)==0);nav_pane_sort(&pane,NAV_SORT_SIZE);assert(pane.listing.count==4);assert(pane.listing.items[0].flags&NAV_ENTRY_PARENT);assert(!strcmp(pane.listing.items[1].name,"small.txt"));nav_pane_sort(&pane,NAV_SORT_NAME);assert(!strcmp(pane.listing.items[1].name,"copy.bin"));nav_listing_free(&pane.listing);
    nav_history_push(&history,"/one");nav_history_push(&history,"/two");assert(!strcmp(nav_history_back(&history),"/one"));nav_history_push(&history,"/three");assert(nav_history_forward(&history)==NULL);
    assert(unlink(small)==0);assert(unlink(destination)==0);assert(unlink(source)==0);assert(rmdir(root)==0);return 0;
}
