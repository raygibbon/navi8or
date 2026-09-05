#include "nav.h"
#include "nav_terminal.h"
#include "nav_theme.h"
#include "nav_ui.h"
#include "tdxui.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { CMD_NONE, CMD_VIEW, CMD_EDIT, CMD_COPY, CMD_MOVE, CMD_DELETE,
               CMD_MKDIR, CMD_QUIT, CMD_REFRESH, CMD_HIDDEN, CMD_SORT_NAME,
               CMD_SORT_SIZE, CMD_SORT_DATE, CMD_FILTER, CMD_HELP, CMD_ABOUT } Command;

static void draw_app(void *data);
static void dispatch(NavApp *app, Command command);

static void set_status(NavApp *app,const char *message){snprintf(app->status,sizeof app->status," %.*s",(int)sizeof app->status-2,message);}

static bool entry_visible(const NavPane *pane,const NavEntry *entry){return pane->filter[0]==0||strstr(entry->name,pane->filter)!=NULL;}

static void draw_pane(NavPane *pane,int x,int y,int width,int height,bool active){
    char title[NAV_PATH_MAX+32],row[512];
    int visible=0,line=0;
    nav_pane_ensure_visible(pane,height-2);
    snprintf(title,sizeof title," %s [%s] ",pane->path,pane->sort_mode==NAV_SORT_SIZE?"size":pane->sort_mode==NAV_SORT_DATE?"date":"name");
    nav_ui_box(x,y,width,height,title,active?NAV_STYLE_ACTIVE:NAV_STYLE_NORMAL);
    for(size_t i=0;i<pane->listing.count&&line<height-2;i++){
        NavEntry *entry=&pane->listing.items[i];
        if(!entry_visible(pane,entry))continue;
        if(visible++<pane->offset)continue;
        snprintf(row,sizeof row,"%-*.*s",width-2,width-2,entry->name);
        nav_ui_text(x+1,y+1+line++,width-2,row,active&&visible-1==pane->selected?NAV_STYLE_SELECTED:NAV_STYLE_NORMAL);
    }
    if(nav_pane_visible_count(pane)==0)nav_ui_text(x+2,y+2,width-4,"(no matches)",NAV_STYLE_NORMAL);
}

static void draw_app(void *data){
    NavApp *app=data;
    int width=nav_term_width(),height=nav_term_height(),pane_height=height-3;
    nav_term_clear(NAV_STYLE_NORMAL);
    if(width<20||height<8){nav_ui_text(0,0,width,"Terminal too small",NAV_STYLE_MENU);return;}
    draw_pane(&app->panes[0],0,0,width/2,pane_height,app->active==0);
    draw_pane(&app->panes[1],width/2,0,width-width/2,pane_height,app->active==1);
    if(app->panes[app->active].filter[0]){char filter[300];snprintf(filter,sizeof filter," Filter: %s",app->panes[app->active].filter);nav_ui_text(0,height-3,width,filter,NAV_STYLE_DIALOG);}else{const NavEntry*entry=nav_pane_selected(&app->panes[app->active]);if(entry){char status[512],date[32];struct tm tm_value;localtime_r(&entry->modified,&tm_value);strftime(date,sizeof date,"%Y-%m-%d %H:%M",&tm_value);snprintf(status,sizeof status," %.*s   %llu bytes   %s",420,entry->path,(unsigned long long)entry->size,date);nav_ui_text(0,height-3,width,status,NAV_STYLE_NORMAL);}else nav_ui_text(0,height-3,width,app->status,NAV_STYLE_NORMAL);}
    nav_ui_text(0,height-2,width," F1 Help  F3 View  F4 Edit  F5 Copy  F6 Move  F7 MkDir  F8 Delete",NAV_STYLE_MENU);
    nav_ui_text(0,height-1,width," Ctrl+\\ Menu   F9 alias   F10 Quit   Tab Switch   / Filter",NAV_STYLE_MENU);
    nav_term_hide_cursor();
}

static void present_app(NavApp *app){draw_app(app);nav_term_present();}

static void restore_selection(NavPane *pane,const char *path){int visible=0;for(size_t i=0;i<pane->listing.count;i++)if(entry_visible(pane,&pane->listing.items[i])){if(!strcmp(path,pane->listing.items[i].path)){pane->selected=visible;break;}visible++;}nav_pane_clamp_selection(pane);nav_pane_ensure_visible(pane,nav_term_height()-5);}

static void refresh_pane(NavApp *app,NavPane *pane){char selected[NAV_PATH_MAX]={0},path[NAV_PATH_MAX],error[256];const NavEntry *entry=nav_pane_selected(pane);if(entry)snprintf(selected,sizeof selected,"%s",entry->path);snprintf(path,sizeof path,"%s",pane->path);if(nav_pane_load(pane,path,app->show_hidden,false,error,sizeof error)){set_status(app,error);return;}nav_pane_sort(pane,pane->sort_mode);if(selected[0])restore_selection(pane,selected);}

static void show_help(void){
    static const char *lines[]={
        "NAV Keys","","Pane navigation",
        "  Tab          Switch pane",
        "  Up/Down      Move selection",
        "  Home/End     First/last item",
        "  PgUp/PgDn    Page movement",
        "  Enter        Open",
        "  Backspace    Parent",
        "  Alt+Left     History back",
        "  Alt+Right    History forward",
        "  Alt+Up       Parent","","Files",
        "  F3           View",
        "  F4           Edit in TDX",
        "  F5           Copy",
        "  F6           Move/Rename",
        "  F7           Make directory",
        "  F8           Delete",
        "  F10          Quit","","Menus",
        "  Ctrl+\\       Open menu (F9 alias)",
        "  Ctrl+Right   Next top-level menu",
        "  Ctrl+Left    Previous top-level menu",
        "  Up/Down      Select command",
        "  Enter        Run command",
        "  Esc          Close menu","","Viewer and Help",
        "  /            Find/filter",
        "  Home/End     Top/bottom",
        "  PgUp/PgDn    Scroll by page",
        "  Esc          Return"
    };
    nav_info(" Help ",lines,sizeof lines/sizeof *lines);
}

typedef struct { NavApp *app; const char *name; uint64_t done,total; } ProgressContext;
static void draw_progress(uint64_t done,uint64_t total,void *data){ProgressContext*c=data;NavTermEvent event;while(nav_term_poll_event(&event,0)>0){if(event.type==NAV_TERM_EVENT_RESIZE)continue;}int width=nav_term_width(),height=nav_term_height(),box_width=width>64?64:width-2,bar_width=box_width-8,percent=total?(int)(done*100/total):100,filled=total?(int)(done*(uint64_t)bar_width/total):bar_width;char line[256],bar[128];c->done=done;c->total=total;draw_app(c->app);if(width<20||height<8){nav_term_present();return;}nav_ui_box((width-box_width)/2,height/2-3,box_width,7," Copying ",NAV_STYLE_DIALOG);nav_ui_text((width-box_width)/2+2,height/2-2,box_width-4,c->name,NAV_STYLE_DIALOG);memset(bar,'-',(size_t)bar_width);for(int i=0;i<filled&&i<bar_width;i++)bar[i]='=';bar[bar_width]=0;snprintf(line,sizeof line,"[%s] %d%%",bar,percent);nav_ui_text((width-box_width)/2+2,height/2,box_width-4,line,NAV_STYLE_DIALOG);snprintf(line,sizeof line,"%llu / %llu bytes",(unsigned long long)done,(unsigned long long)total);nav_ui_text((width-box_width)/2+2,height/2+2,box_width-4,line,NAV_STYLE_DIALOG);nav_term_present();}

static void command_copy(NavApp *app){NavPane*source=&app->panes[app->active],*destination=&app->panes[!app->active];const NavEntry*entry=nav_pane_selected(source);char target[NAV_PATH_MAX],label[NAV_PATH_MAX+32],error[256];NavEntry existing;bool overwrite=false;if(!entry){set_status(app,"No selected entry");return;}if(entry->flags&NAV_ENTRY_DIR){set_status(app,"Directory copy is not implemented");return;}if(nav_path_join(destination->path,nav_path_basename(entry->path),target,sizeof target)){set_status(app,"Destination path is too long");return;}snprintf(label,sizeof label,"To: ");if(nav_prompt_text(" Copy ",label,target,sizeof target,draw_app,app))return;if(!destination->provider->stat(destination->provider,target,&existing,error,sizeof error)){char question[NAV_NAME_MAX+32];snprintf(question,sizeof question,"Overwrite \"%.*s\"?",NAV_NAME_MAX-16,existing.name);if(!nav_confirm(question,draw_app,app))return;overwrite=true;}ProgressContext progress={app,entry->name,0,entry->size};if(nav_transfer_copy(source->provider,entry->path,destination->provider,target,overwrite,draw_progress,&progress,error,sizeof error))set_status(app,error);else set_status(app,"Copy complete");refresh_pane(app,destination);}

static void command_move(NavApp *app){NavPane*source=&app->panes[app->active],*destination=&app->panes[!app->active];const NavEntry*entry=nav_pane_selected(source);char target[NAV_PATH_MAX],error[256];if(!entry){set_status(app,"No selected entry");return;}if(nav_path_join(destination->path,nav_path_basename(entry->path),target,sizeof target)){set_status(app,"Destination path is too long");return;}if(nav_prompt_text(" Move / Rename ","To: ",target,sizeof target,draw_app,app))return;if(source->provider->rename_path(source->provider,entry->path,target,error,sizeof error))set_status(app,error);else set_status(app,"Move complete");refresh_pane(app,source);refresh_pane(app,destination);}

static void command_mkdir(NavApp *app){NavPane*pane=&app->panes[app->active];char name[NAV_NAME_MAX]={0},path[NAV_PATH_MAX],error[256];if(nav_prompt_text(" Create Directory ","Name: ",name,sizeof name,draw_app,app)||!name[0])return;if(nav_path_join(pane->path,name,path,sizeof path)){set_status(app,"Directory path is too long");return;}if(pane->provider->mkdir(pane->provider,path,error,sizeof error))set_status(app,error);else set_status(app,"Directory created");refresh_pane(app,pane);}

static void command_delete(NavApp *app){NavPane*pane=&app->panes[app->active];const NavEntry*entry=nav_pane_selected(pane);char question[NAV_NAME_MAX+32],error[256];if(!entry||entry->flags&NAV_ENTRY_PARENT)return;snprintf(question,sizeof question,"Delete \"%.*s\"?",NAV_NAME_MAX-16,entry->name);if(!nav_confirm(question,draw_app,app))return;if(pane->provider->remove(pane->provider,entry->path,error,sizeof error))set_status(app,error);else set_status(app,"Deleted");refresh_pane(app,pane);}

static void command_filter(NavApp *app){NavPane*pane=&app->panes[app->active];char original[NAV_NAME_MAX];snprintf(original,sizeof original,"%s",pane->filter);if(nav_prompt_text(" Filter ","Filter: ",pane->filter,sizeof pane->filter,draw_app,app)){snprintf(pane->filter,sizeof pane->filter,"%s",original);}nav_pane_clamp_selection(pane);nav_pane_ensure_visible(pane,nav_term_height()-5);}

static void command_edit(NavApp *app){const NavEntry*entry=nav_pane_selected(&app->panes[app->active]);char error[256];if(!entry||entry->flags&NAV_ENTRY_DIR)return;nav_term_shutdown();if(nav_platform_launch_tdx(entry->path,error,sizeof error))set_status(app,error);nav_term_set_theme(nav_theme_classic_dos());if(nav_term_init()<0){set_status(app,"Unable to resume terminal");app->running=false;}}

static void dispatch(NavApp *app,Command command){NavPane*pane=&app->panes[app->active];switch(command){case CMD_VIEW:{const NavEntry*entry=nav_pane_selected(pane);if(entry&&!(entry->flags&NAV_ENTRY_DIR))nav_view_file(entry);break;}case CMD_EDIT:command_edit(app);break;case CMD_COPY:command_copy(app);break;case CMD_MOVE:command_move(app);break;case CMD_DELETE:command_delete(app);break;case CMD_MKDIR:command_mkdir(app);break;case CMD_QUIT:app->running=false;break;case CMD_REFRESH:refresh_pane(app,pane);break;case CMD_HIDDEN:app->show_hidden=!app->show_hidden;refresh_pane(app,&app->panes[0]);refresh_pane(app,&app->panes[1]);break;case CMD_SORT_NAME:nav_pane_sort(pane,NAV_SORT_NAME);break;case CMD_SORT_SIZE:nav_pane_sort(pane,NAV_SORT_SIZE);break;case CMD_SORT_DATE:nav_pane_sort(pane,NAV_SORT_DATE);break;case CMD_FILTER:command_filter(app);break;case CMD_HELP:show_help();break;case CMD_ABOUT:{const char*lines[]={"NAV 0.1","Keyboard-first local navigator","TDX/TDE interaction and visual conventions"};nav_info(" About NAV ",lines,3);break;}default:break;}}

#define ITEM(label,command,key) {label,command,NULL,false,false,key}
#define DISABLED(label,key) {label,CMD_NONE,NULL,true,false,key}
#define SEPARATOR {NULL,CMD_NONE,NULL,true,true,0}
static const TdxUiMenuItem file_items[]={ITEM("View",CMD_VIEW,'v'),ITEM("Edit",CMD_EDIT,'e'),ITEM("Copy",CMD_COPY,'c'),ITEM("Move/Rename",CMD_MOVE,'m'),ITEM("Delete",CMD_DELETE,'d'),ITEM("Make Directory",CMD_MKDIR,'a'),SEPARATOR,ITEM("Quit",CMD_QUIT,'q')};
static const TdxUiMenuItem view_items[]={ITEM("Refresh",CMD_REFRESH,'r'),ITEM("Show Hidden",CMD_HIDDEN,'h'),SEPARATOR,ITEM("Sort By Name",CMD_SORT_NAME,'n'),ITEM("Sort By Size",CMD_SORT_SIZE,'s'),ITEM("Sort By Date",CMD_SORT_DATE,'d')};
static const TdxUiMenuItem search_items[]={ITEM("Filter",CMD_FILTER,'f'),DISABLED("Find File",'i'),DISABLED("Search Contents",'c')};
static const TdxUiMenuItem unavailable_items[]={DISABLED("Not available in local v0.1",'n')};
static const TdxUiMenuItem help_items[]={ITEM("Keys",CMD_HELP,'k'),ITEM("About NAV",CMD_ABOUT,'a')};
static TdxUiMenu menus[]={{"File",file_items,sizeof file_items/sizeof *file_items,0},{"View",view_items,sizeof view_items/sizeof *view_items,0},{"Search",search_items,sizeof search_items/sizeof *search_items,0},{"Transfer",unavailable_items,1,0},{"Repo",unavailable_items,1,0},{"Vault",unavailable_items,1,0},{"Options",unavailable_items,1,0},{"Help",help_items,sizeof help_items/sizeof *help_items,0}};
static int saved_major_menu;
static void open_menu(NavApp *app){int command=tdxui_pull_down(menus,sizeof menus/sizeof *menus,&saved_major_menu,draw_app,app);if(command>=0)dispatch(app,(Command)command);}

static void navigate_parent(NavApp *app,NavPane *pane,bool history){char parent[NAV_PATH_MAX],error[256]={0};if(nav_path_parent(pane->path,parent,sizeof parent)==0&&nav_pane_load(pane,parent,app->show_hidden,history,error,sizeof error)==0)nav_pane_sort(pane,pane->sort_mode);else if(error[0])set_status(app,error);}
static void activate(NavApp *app,NavPane *pane){const NavEntry*entry=nav_pane_selected(pane);char error[256];if(!entry)return;if(!(entry->flags&NAV_ENTRY_DIR)){dispatch(app,CMD_VIEW);return;}if(entry->flags&NAV_ENTRY_PARENT){navigate_parent(app,pane,true);return;}if(nav_pane_load(pane,entry->path,app->show_hidden,true,error,sizeof error))set_status(app,error);else nav_pane_sort(pane,pane->sort_mode);}

int nav_ui_run(NavApp *app){
    nav_term_set_theme(nav_theme_classic_dos());
    if(nav_term_init()<0){fprintf(stderr,"nav: terminal initialization failed\n");return 1;}
    set_status(app,"Ready");
    while(app->running){
        NavTermEvent event;
        NavPane *pane=&app->panes[app->active];
        present_app(app);
        if(nav_term_poll_event(&event,-1)<=0)continue;
        if(event.type==NAV_TERM_EVENT_RESIZE){for(int i=0;i<2;i++){nav_pane_clamp_selection(&app->panes[i]);nav_pane_ensure_visible(&app->panes[i],nav_term_height()-5);}continue;}
        if(event.type!=NAV_TERM_EVENT_KEY)continue;
        int key=event.key;
        if(key==NAV_KEY_F10)dispatch(app,CMD_QUIT);
        else if(key==NAV_KEY_F1)dispatch(app,CMD_HELP);
        else if(key==NAV_KEY_F3)dispatch(app,CMD_VIEW);
        else if(key==NAV_KEY_F4)dispatch(app,CMD_EDIT);
        else if(key==NAV_KEY_F5)dispatch(app,CMD_COPY);
        else if(key==NAV_KEY_F6)dispatch(app,CMD_MOVE);
        else if(key==NAV_KEY_F7)dispatch(app,CMD_MKDIR);
        else if(key==NAV_KEY_F8)dispatch(app,CMD_DELETE);
        else if(key==NAV_KEY_F9||(key=='\\'&&(event.modifiers&NAV_MOD_CTRL)))open_menu(app);
        else if(key==NAV_KEY_TAB)app->active=!app->active;
        else if(key=='/')dispatch(app,CMD_FILTER);
        else if(key==NAV_KEY_UP&&(event.modifiers&NAV_MOD_ALT))navigate_parent(app,pane,true);
        else if(key==NAV_KEY_LEFT&&(event.modifiers&NAV_MOD_ALT)){const char*path=nav_history_back(&pane->history);char error[256];if(path&&nav_pane_load(pane,path,app->show_hidden,false,error,sizeof error)==0)nav_pane_sort(pane,pane->sort_mode);}
        else if(key==NAV_KEY_RIGHT&&(event.modifiers&NAV_MOD_ALT)){const char*path=nav_history_forward(&pane->history);char error[256];if(path&&nav_pane_load(pane,path,app->show_hidden,false,error,sizeof error)==0)nav_pane_sort(pane,pane->sort_mode);}
        else if(key==NAV_KEY_UP)pane->selected--;
        else if(key==NAV_KEY_DOWN)pane->selected++;
        else if(key==NAV_KEY_HOME)pane->selected=0;
        else if(key==NAV_KEY_END)pane->selected=nav_pane_visible_count(pane)-1;
        else if(key==NAV_KEY_PAGE_UP)pane->selected-=nav_term_height()-5;
        else if(key==NAV_KEY_PAGE_DOWN)pane->selected+=nav_term_height()-5;
        else if(key==NAV_KEY_BACKSPACE)navigate_parent(app,pane,true);
        else if(key==NAV_KEY_ENTER)activate(app,pane);
        nav_pane_clamp_selection(pane);
        nav_pane_ensure_visible(pane,nav_term_height()-5);
    }
    nav_term_shutdown();
    return 0;
}
