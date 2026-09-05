/*
 * TDX-derived transient control conventions: redraw before input, Escape
 * cancels, Enter accepts, and resize invalidates old geometry before redraw.
 */
#include "nav_ui.h"
#include "nav_symbols.h"
#include "tdxui.h"
#include <stdio.h>
#include <string.h>

void nav_ui_text(int x,int y,int width,const char *text,NavStyle style){if(width>0)nav_term_text(x,y,width,text,style);}

void nav_ui_box(int x,int y,int width,int height,const char *title,NavStyle style){
    const NavSymbols *symbols=nav_symbols_classic_dos();
    if(width<4||height<3)return;
    nav_term_glyph(x,y,symbols->upper_left,style);
    nav_term_hline(x+1,y,symbols->horizontal,width-2,style);
    nav_term_glyph(x+width-1,y,symbols->upper_right,style);
    nav_term_vline(x,y+1,symbols->vertical,height-2,style);
    nav_term_vline(x+width-1,y+1,symbols->vertical,height-2,style);
    nav_term_glyph(x,y+height-1,symbols->lower_left,style);
    nav_term_hline(x+1,y+height-1,symbols->horizontal,width-2,style);
    nav_term_glyph(x+width-1,y+height-1,symbols->lower_right,style);
    if(title)nav_ui_text(x+2,y,width-4,title,style);
}

int nav_prompt_text(const char *title,const char *label,char *buffer,size_t capacity,
                    NavUiRedrawFn redraw,void *data){
    TdxUiField field;
    const char *prompt_label=label?label:"";
    tdxui_field_init(&field,buffer,capacity);
    for(;;){
        NavTermEvent event;
        int width=nav_term_width(),height=nav_term_height();
        int box_width=width>70?70:width-2;
        int x=(width-box_width)/2,y=height/2-2,cursor,field_width;
        char line[512];
        if(redraw)redraw(data);else nav_term_clear(NAV_STYLE_NORMAL);
        if(width>=20&&height>=8){
            nav_ui_box(x,y,box_width,5,title,NAV_STYLE_DIALOG);
            field_width=box_width-4-(int)strlen(prompt_label);
            if(field_width<1)field_width=1;
            tdxui_field_ensure_visible(&field,(size_t)field_width);
            snprintf(line,sizeof line,"%s%s",prompt_label,buffer+field.offset);
            nav_ui_text(x+2,y+2,box_width-4,line,NAV_STYLE_DIALOG);
            cursor=x+2+(int)strlen(prompt_label)+(int)(field.cursor-field.offset);
            if(cursor>x+box_width-2)cursor=x+box_width-2;
            nav_term_cursor(cursor,y+2);
        }else nav_term_hide_cursor();
        nav_term_present();
        if(nav_term_poll_event(&event,-1)<=0)continue;
        if(event.type==NAV_TERM_EVENT_RESIZE)continue;
        TdxUiFieldResult result=tdxui_field_event(&field,&event);
        if(result==TDXUI_FIELD_CANCELLED){nav_term_hide_cursor();return -1;}
        if(result==TDXUI_FIELD_ACCEPTED){nav_term_hide_cursor();return 0;}
    }
}

bool nav_confirm(const char *question,NavUiRedrawFn redraw,void *data){
    bool yes=false;
    for(;;){
        NavTermEvent event;
        int width=nav_term_width(),height=nav_term_height();
        int box_width=width>52?52:width-2,x=(width-box_width)/2,y=height/2-2;
        if(redraw)redraw(data);else nav_term_clear(NAV_STYLE_NORMAL);
        if(width>=20&&height>=8){
            nav_ui_box(x,y,box_width,5," Confirm ",NAV_STYLE_DIALOG);
            nav_ui_text(x+2,y+1,box_width-4,question,NAV_STYLE_DIALOG);
            nav_ui_text(x+box_width/2-10,y+3,7,"[ Yes ]",yes?NAV_STYLE_SELECTED:NAV_STYLE_DIALOG);
            nav_ui_text(x+box_width/2+2,y+3,6,"[ No ]",yes?NAV_STYLE_DIALOG:NAV_STYLE_SELECTED);
        }
        nav_term_hide_cursor();nav_term_present();
        if(nav_term_poll_event(&event,-1)<=0)continue;
        if(event.type==NAV_TERM_EVENT_RESIZE)continue;
        if(event.type!=NAV_TERM_EVENT_KEY)continue;
        if(event.key==NAV_KEY_ESCAPE)return false;
        if(event.key==NAV_KEY_LEFT||event.key==NAV_KEY_RIGHT)yes=!yes;
        else if(event.key=='y'||event.key=='Y')yes=true;
        else if(event.key=='n'||event.key=='N')yes=false;
        else if(event.key==NAV_KEY_ENTER)return yes;
    }
}

void nav_info(const char *title,const char *const *lines,size_t count){
    tdxui_info(title,lines,count,NULL,NULL);
}
