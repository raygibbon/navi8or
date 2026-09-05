/*
 * Adapted from TDX help.c/hwind.c window navigation and clipping concepts.
 * NAV supplies content while this control owns scrolling and resize redraws.
 */
#include "tdxui.h"

void tdxui_info(const char *title,const char *const *lines,size_t count,
                NavUiRedrawFn redraw,void *data){
    TdxUiTextView view={0};
    for(;;){
        NavTermEvent event;
        int width=nav_term_width(),height=nav_term_height();
        int box_width=width>74?74:width-2;
        int box_height=(int)count+4;
        size_t page;
        if(box_height>height)box_height=height;
        page=box_height>=4?(size_t)box_height-3:0;
        tdxui_text_view_clamp(&view,count,page);
        if(redraw)redraw(data);else nav_term_clear(NAV_STYLE_NORMAL);
        if(width>=20&&height>=8){
            int x=(width-box_width)/2,y=(height-box_height)/2;
            nav_ui_box(x,y,box_width,box_height,title,NAV_STYLE_DIALOG);
            for(size_t row=0;row<page&&view.top+row<count;row++)
                nav_ui_text(x+2,y+1+(int)row,box_width-4,
                            lines[view.top+row],NAV_STYLE_DIALOG);
            nav_ui_text(x+2,y+box_height-2,box_width-4,
                        "Esc closes  Up/Down PgUp/PgDn Home/End",NAV_STYLE_MENU);
        }else nav_ui_text(0,0,width,"Terminal too small",NAV_STYLE_MENU);
        nav_term_hide_cursor();nav_term_present();
        if(nav_term_poll_event(&event,-1)<0)continue;
        if(event.type==NAV_TERM_EVENT_RESIZE)continue;
        if(event.type!=NAV_TERM_EVENT_KEY)continue;
        if(event.key==NAV_KEY_ESCAPE)return;
        tdxui_text_view_key(&view,event.key,count,page);
    }
}
