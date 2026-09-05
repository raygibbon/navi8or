/* NAV presentation over a strictly read-only NavViewSource. */
#include "nav_ui.h"
#include "nav_view.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { NavViewer viewer; const NavEntry *entry; } ViewerScreen;

static size_t viewer_page(void){int height=nav_term_height();return height>3?(size_t)height-3:1;}

static void binary_info(const NavEntry *entry){
    char name[300],path[NAV_PATH_MAX+16],size[128],modified[128],timebuf[64];
    struct tm tm_value;
    localtime_r(&entry->modified,&tm_value);strftime(timebuf,sizeof timebuf,"%Y-%m-%d %H:%M",&tm_value);
    snprintf(name,sizeof name,"Name:      %s",entry->name);
    snprintf(path,sizeof path,"Path:      %s",entry->path);
    snprintf(size,sizeof size,"Size:      %llu bytes",(unsigned long long)entry->size);
    snprintf(modified,sizeof modified,"Modified:  %s",timebuf);
    const char *lines[]={name,path,size,modified,"Type:      Binary","","NAV does not interpret or modify binary content."};
    nav_info(" File Information ",lines,sizeof lines/sizeof *lines);
}

static size_t line_span(NavViewer *viewer,size_t line,size_t width){
    size_t length=viewer->source->line_length(viewer->source,line);
    if(!width)return 1;
    return length?(length+width-1)/width:1;
}

static void ensure_wrapped(NavViewer *viewer,size_t width,size_t page){
    size_t top=viewer->current_line,used=line_span(viewer,top,width);
    while(top>0){size_t span=line_span(viewer,top-1,width);if(used+span>page)break;used+=span;top--;}
    viewer->top_line=top;
}

static void draw_fragment(NavViewer *viewer,const char *line,size_t length,
                          size_t logical,size_t start,int x,int y,int width,
                          NavStyle style){
    size_t available=start<length?length-start:0;
    int amount=available<(size_t)width?(int)available:width;
    nav_ui_text(x,y,width,amount?line+start:"",style);
    if(logical==viewer->match_line&&viewer->match_length){
        size_t match_start=viewer->match_column,match_end=match_start+viewer->match_length;
        size_t visible_end=start+(size_t)width;
        size_t from=match_start>start?match_start:start;
        size_t to=match_end<visible_end?match_end:visible_end;
        if(from<to&&from<length){
            if(to>length)to=length;
            nav_ui_text(x+(int)(from-start),y,(int)(to-from),line+from,NAV_STYLE_SEARCH_MATCH);
        }
    }
}

static void draw_viewer(void *data){
    ViewerScreen *screen=data;NavViewer *viewer=&screen->viewer;
    size_t count=viewer->source->line_count(viewer->source);
    int width=nav_term_width(),height=nav_term_height();
    nav_term_clear(NAV_STYLE_NORMAL);
    if(width<20||height<8){nav_ui_text(0,0,width,"Terminal too small",NAV_STYLE_MENU);nav_term_hide_cursor();return;}
    size_t gutter=nav_viewer_line_number_width(viewer),page=viewer_page();
    int text_width=width-(int)gutter;if(text_width<1)text_width=1;
    if(viewer->wrap)ensure_wrapped(viewer,(size_t)text_width,page);
    else nav_viewer_ensure_visible(viewer,page);
    nav_ui_text(0,0,width,screen->entry->path,NAV_STYLE_MENU);
    size_t logical=viewer->top_line;int row=1;
    while(logical<count&&row<height-2){
        size_t length=0;const char *line=viewer->source->line(viewer->source,logical,&length);
        if(!line)line="";
        size_t segments=viewer->wrap?line_span(viewer,logical,(size_t)text_width):1;
        for(size_t segment=0;segment<segments&&row<height-2;segment++,row++){
            NavStyle style=logical==viewer->current_line?NAV_STYLE_VIEW_CURRENT:NAV_STYLE_NORMAL;
            if(gutter){
                char number[32]={0},digits[24];size_t digit_count;
                memset(number,' ',gutter<sizeof number-1?gutter:sizeof number-1);
                if(segment==0){
                    number[0]=logical==viewer->current_line?'>':' ';
                    snprintf(digits,sizeof digits,"%zu",logical+1);digit_count=strlen(digits);
                    if(digit_count+1<gutter)memcpy(number+gutter-1-digit_count,digits,digit_count);
                }
                nav_ui_text(0,row,(int)gutter,number,style);
            }
            size_t start=viewer->wrap?segment*(size_t)text_width:viewer->horizontal_offset;
            draw_fragment(viewer,line,length,logical,start,(int)gutter,row,text_width,style);
        }
        logical++;
    }
    char status[512];size_t shown=count?viewer->current_line+1:0;
    unsigned percent=count?(unsigned)(shown*100/count):0;
    snprintf(status,sizeof status," Ln %zu/%zu  Col %zu  %u%%  %s  %s%s%s",
             shown,count,viewer->horizontal_offset+1,percent,
             viewer->wrap?"Wrap":"No Wrap",screen->entry->name,
             viewer->status[0]?"  ":"",viewer->status);
    nav_ui_text(0,height-2,width,status,NAV_STYLE_DIALOG);
    nav_ui_text(0,height-1,width," Esc Back  / Find  F5 Next  F6 Prev  g GoTo  w Wrap  l Lines",NAV_STYLE_MENU);
    nav_term_hide_cursor();
}

static void find_prompt(ViewerScreen *screen){
    char original[NAV_SEARCH_MAX];bool wrapped=false;
    snprintf(original,sizeof original,"%s",screen->viewer.search);
    if(nav_prompt_text(" Find ","Find: ",screen->viewer.search,
                       sizeof screen->viewer.search,draw_viewer,screen)!=0){
        snprintf(screen->viewer.search,sizeof screen->viewer.search,"%s",original);return;
    }
    nav_viewer_find(&screen->viewer,1,viewer_page(),&wrapped);
}

static void goto_prompt(ViewerScreen *screen){
    char answer[32],*end;unsigned long long line;
    snprintf(answer,sizeof answer,"%zu",screen->viewer.current_line+1);
    if(nav_prompt_text(" Go To Line ","Line: ",answer,sizeof answer,draw_viewer,screen)!=0)return;
    line=strtoull(answer,&end,10);
    if(end==answer||*end){snprintf(screen->viewer.status,sizeof screen->viewer.status,"Invalid line number");return;}
    nav_viewer_goto_line(&screen->viewer,(size_t)line,viewer_page());
    screen->viewer.match_line=SIZE_MAX;screen->viewer.match_length=0;
    snprintf(screen->viewer.status,sizeof screen->viewer.status,"Line %zu",screen->viewer.current_line+1);
}

void nav_view_file(const NavEntry *entry){
    char error[256]={0};bool binary=false,wrapped=false;
    NavViewSource *source=nav_view_source_open_local(entry->path,&binary,error,sizeof error);
    if(!source){
        if(binary)binary_info(entry);
        else {const char *lines[]={error[0]?error:"Unable to open file"};nav_info(" Viewer Error ",lines,1);}
        return;
    }
    ViewerScreen screen={.entry=entry};nav_viewer_init(&screen.viewer,source);
    for(;;){
        NavTermEvent event;size_t page=viewer_page();
        draw_viewer(&screen);nav_term_present();
        if(nav_term_poll_event(&event,-1)<=0)continue;
        if(event.type==NAV_TERM_EVENT_RESIZE)continue;
        if(event.type!=NAV_TERM_EVENT_KEY)continue;
        if(event.key==NAV_KEY_ESCAPE)break;
        if(event.key==NAV_KEY_UP&&event.modifiers==0)nav_viewer_move(&screen.viewer,-1,page);
        else if(event.key==NAV_KEY_DOWN&&event.modifiers==0)nav_viewer_move(&screen.viewer,1,page);
        else if(event.key==NAV_KEY_PAGE_UP&&event.modifiers==0)nav_viewer_page(&screen.viewer,-1,page);
        else if(event.key==NAV_KEY_PAGE_DOWN&&event.modifiers==0)nav_viewer_page(&screen.viewer,1,page);
        else if((event.key==NAV_KEY_HOME&&event.modifiers==0)||(event.key==NAV_KEY_HOME&&(event.modifiers&NAV_MOD_CTRL)))nav_viewer_top(&screen.viewer);
        else if((event.key==NAV_KEY_END&&event.modifiers==0)||(event.key==NAV_KEY_END&&(event.modifiers&NAV_MOD_CTRL)))nav_viewer_bottom(&screen.viewer,page);
        else if(!screen.viewer.wrap&&event.key==NAV_KEY_LEFT)nav_viewer_horizontal(&screen.viewer,event.modifiers&NAV_MOD_CTRL?-8:-1);
        else if(!screen.viewer.wrap&&event.key==NAV_KEY_RIGHT)nav_viewer_horizontal(&screen.viewer,event.modifiers&NAV_MOD_CTRL?8:1);
        else if(event.key=='/'&&event.modifiers==0)find_prompt(&screen);
        else if(event.key==NAV_KEY_F5)nav_viewer_find(&screen.viewer,1,page,&wrapped);
        else if(event.key==NAV_KEY_F6)nav_viewer_find(&screen.viewer,-1,page,&wrapped);
        else if((event.key=='g'||event.key=='G')&&event.modifiers==0)goto_prompt(&screen);
        else if((event.key=='w'||event.key=='W')&&event.modifiers==0){screen.viewer.wrap=!screen.viewer.wrap;screen.viewer.horizontal_offset=0;snprintf(screen.viewer.status,sizeof screen.viewer.status,"Wrap %s",screen.viewer.wrap?"on":"off");}
        else if((event.key=='l'||event.key=='L')&&event.modifiers==0){screen.viewer.line_numbers=!screen.viewer.line_numbers;snprintf(screen.viewer.status,sizeof screen.viewer.status,"Line numbers %s",screen.viewer.line_numbers?"on":"off");}
    }
    source->close(source);
}
