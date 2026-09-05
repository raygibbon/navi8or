/*
 * Adapted from TDX src/core/pull.c lite_bar_menu()/pull_me().
 * Editor-global command lookup and saved-screen buffers are removed for NAV;
 * major/minor state, wrapping, accelerators, clipping, and resize exit remain.
 */
#include "tdxui.h"
#include "nav_symbols.h"
#include <stdio.h>
#include <string.h>

static size_t first_selectable(const TdxUiMenu *menu){
    for(size_t i=0;i<menu->minor_count;i++)if(!menu->minor[i].separator)return i;
    return menu->minor_count;
}

static int heading_column(const TdxUiMenu *menus,size_t major){
    int column=1;
    for(size_t i=0;i<major;i++)column+=(int)strlen(menus[i].label)+3;
    return column;
}

static void draw_headings(const TdxUiMenu *menus,size_t count,size_t selected){
    int column=1,width=nav_term_width();
    nav_ui_text(0,0,width,"",NAV_STYLE_MENU);
    for(size_t i=0;i<count&&column<width;i++){
        int length=(int)strlen(menus[i].label);
        nav_ui_text(column,0,length,menus[i].label,
                    i==selected?NAV_STYLE_SELECTED:NAV_STYLE_MENU);
        column+=length+3;
    }
}

static size_t viewport_top(const TdxUiMenu *menu,size_t selected,size_t visible){
    size_t top=0;
    if(visible&&selected>=visible)top=selected-visible+1;
    if(top+visible>menu->minor_count&&menu->minor_count>visible)
        top=menu->minor_count-visible;
    return top;
}

int tdxui_pull_down(TdxUiMenu *menus,size_t count,int *saved_major,
                    NavUiRedrawFn redraw,void *data){
    size_t major;
    if(!menus||!count||!saved_major)return TDXUI_MENU_CANCELLED;
    major=*saved_major>=0&&(size_t)*saved_major<count?(size_t)*saved_major:0;
    for(;;){
        TdxUiMenu *menu=&menus[major];
        size_t selected=menu->current<menu->minor_count&&
                        !menu->minor[menu->current].separator?
                        menu->current:first_selectable(menu);
        int screen_width=nav_term_width(),screen_height=nav_term_height();
        int menu_width=4,column=heading_column(menus,major);
        size_t visible,top;
        for(size_t i=0;i<menu->minor_count;i++){
            int length=menu->minor[i].line?(int)strlen(menu->minor[i].line):0;
            if(length+4>menu_width)menu_width=length+4;
        }
        if(menu_width>screen_width)menu_width=screen_width;
        if(column+menu_width>screen_width)column=screen_width-menu_width;
        if(column<0)column=0;
        visible=screen_height>3?(size_t)screen_height-3:0;
        if(visible>menu->minor_count)visible=menu->minor_count;
        top=viewport_top(menu,selected,visible);
        if(redraw)redraw(data);else nav_term_clear(NAV_STYLE_NORMAL);
        if(screen_width>=20&&screen_height>=8){
            draw_headings(menus,count,major);
            nav_ui_box(column,1,menu_width,(int)visible+2,menu->label,NAV_STYLE_DIALOG);
            for(size_t row=0;row<visible;row++){
                size_t i=top+row;
                const TdxUiMenuItem *item=&menu->minor[i];
                if(item->separator){
                    nav_term_hline(column+1,2+(int)row,
                                   nav_symbols_classic_dos()->separator,
                                   menu_width-2,NAV_STYLE_DIALOG);
                    if(item->line)nav_ui_text(column+2,2+(int)row,
                                              menu_width-4,item->line,
                                              NAV_STYLE_DIALOG);
                    continue;
                }
                char line[128];
                snprintf(line,sizeof line,"%s%s",item->disabled?"- ":"",item->line);
                nav_ui_text(column+1,2+(int)row,menu_width-2,line,
                            i==selected?NAV_STYLE_SELECTED:NAV_STYLE_DIALOG);
            }
        }
        nav_term_hide_cursor();nav_term_present();
        NavTermEvent event;
        if(nav_term_poll_event(&event,-1)<=0)continue;
        if(event.type==NAV_TERM_EVENT_RESIZE){
            menu->current=selected;*saved_major=(int)major;
            return TDXUI_MENU_RESIZED;
        }
        if(event.type!=NAV_TERM_EVENT_KEY)continue;
        if(event.key==NAV_KEY_ESCAPE){
            menu->current=selected;*saved_major=(int)major;
            return TDXUI_MENU_CANCELLED;
        }
        int major_motion=tdxui_menu_major_motion(&event);
        if(major_motion){
            menu->current=selected;
            major=tdxui_menu_move_major(major,count,major_motion);
            continue;
        }
        if(event.key==NAV_KEY_DOWN&&event.modifiers==0){
            selected=tdxui_menu_move_minor(menu,selected,1);
            menu->current=selected;continue;
        }
        if(event.key==NAV_KEY_UP&&event.modifiers==0){
            selected=tdxui_menu_move_minor(menu,selected,-1);
            menu->current=selected;continue;
        }
        if(event.modifiers==0&&event.key>='1'&&event.key<='9'){
            size_t requested=(size_t)(event.key-'1');
            if(requested<count){menu->current=selected;major=requested;continue;}
        }
        if(event.key==NAV_KEY_ENTER&&event.modifiers==0&&selected<menu->minor_count){
            const TdxUiMenuItem *item=&menu->minor[selected];
            menu->current=selected;*saved_major=(int)major;
            if(!item->disabled&&!item->separator)return item->command;
            continue;
        }
        if(event.modifiers==0){
            size_t accelerated=selected;
            int command=tdxui_menu_accelerator(menu,event.key,&accelerated);
            if(command!=TDXUI_MENU_CANCELLED){
                menu->current=accelerated;*saved_major=(int)major;
                return command;
            }
        }
    }
}
