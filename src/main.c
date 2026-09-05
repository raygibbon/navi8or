#include "nav.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc,char **argv){
    NavApp app;
    char cwd[NAV_PATH_MAX],error[256];
    const char *left,*right;
    int result;
    if(argc>1&&!strcmp(argv[1],"--help")){puts("Usage: nav [left-directory] [right-directory]\nKeyboard-first dual-pane local file navigator.");return 0;}
    memset(&app,0,sizeof app);
    if(!getcwd(cwd,sizeof cwd)){perror("nav");return 1;}
    left=argc>1?argv[1]:cwd;
    right=argc>2?argv[2]:cwd;
    if((!strncmp(left,"http://",7)||!strncmp(left,"https://",8))||(!strncmp(right,"http://",7)||!strncmp(right,"https://",8))){fprintf(stderr,"nav: HTTP providers are planned but not implemented\n");return 2;}
    for(int i=0;i<2;i++){app.panes[i].provider=nav_local_provider();app.panes[i].history.current=-1;}
    if(nav_pane_load(&app.panes[0],left,false,true,error,sizeof error)||nav_pane_load(&app.panes[1],right,false,true,error,sizeof error)){fprintf(stderr,"nav: %s\n",error);nav_listing_free(&app.panes[0].listing);nav_listing_free(&app.panes[1].listing);return 1;}
    app.running=true;
    result=nav_ui_run(&app);
    nav_listing_free(&app.panes[0].listing);
    nav_listing_free(&app.panes[1].listing);
    return result;
}
