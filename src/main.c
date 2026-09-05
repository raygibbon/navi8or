#include "nav.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc,char **argv){NavApp app;char cwd[NAV_PATH_MAX],err[256];const char *left,*right;if(argc>1&&!strcmp(argv[1],"--help")){puts("Usage: nav [local-directory]\nKeyboard-first dual-pane local file navigator.");return 0;}memset(&app,0,sizeof app);if(!getcwd(cwd,sizeof cwd)){perror("nav");return 1;}left=argc>1?argv[1]:cwd;right=cwd;if(argc>2&&(!strncmp(argv[2],"http://",7)||!strncmp(argv[2],"https://",8))){fprintf(stderr,"nav: HTTP providers are planned but not implemented\n");return 2;}if(!strncmp(left,"http://",7)||!strncmp(left,"https://",8)){fprintf(stderr,"nav: HTTP providers are planned but not implemented\n");return 2;}for(int i=0;i<2;i++){app.panes[i].provider=nav_local_provider();app.panes[i].history.current=-1;}if(nav_pane_load(&app.panes[0],left,false,true,err,sizeof err)||nav_pane_load(&app.panes[1],right,false,true,err,sizeof err)){fprintf(stderr,"nav: %s\n",err);return 1;}app.running=true;return nav_ui_run(&app);}
