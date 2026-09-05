/*
 * Derived from TDX src/terminal/termbox_backend.c.
 * Raw termbox events, modifier translation, CP437 conversion, and resize
 * invalidation stay at this boundary. Persistent UI policy belongs above it.
 */
#define TB_IMPL
#include "termbox2/termbox2.h"
#include "cp437.h"
#include "nav_terminal.h"
#include "nav_theme.h"
#include <locale.h>
#include <string.h>

static const uintattr_t dos_palette[16]={16,19,34,37,124,127,130,145,240,21,46,51,196,201,226,231};
static const NavTheme *active_theme;
static int terminal_active;

void nav_term_set_theme(const NavTheme *theme){active_theme=theme;}

static void put_cell(int x,int y,uint32_t ch,NavStyle style){
    if(!terminal_active||!active_theme||x<0||y<0||x>=tb_width()||y>=tb_height())return;
    tb_set_cell(x,y,ch,dos_palette[active_theme->foreground[style]],dos_palette[active_theme->background[style]]);
}

int nav_term_init(void){
    int result;
    setlocale(LC_CTYPE,"");
    if(!active_theme)return -1;
    result=tb_init();
    if(result<0)return result;
    terminal_active=1;
    if(tb_set_output_mode(TB_OUTPUT_256)<0||tb_set_input_mode(TB_INPUT_ALT)<0){nav_term_shutdown();return -1;}
    return 0;
}

void nav_term_shutdown(void){if(terminal_active){tb_shutdown();terminal_active=0;}}
int nav_term_width(void){return terminal_active?tb_width():0;}
int nav_term_height(void){return terminal_active?tb_height():0;}
void nav_term_clear(NavStyle style){if(terminal_active&&active_theme){tb_set_clear_attrs(dos_palette[active_theme->foreground[style]],dos_palette[active_theme->background[style]]);tb_clear();}}
void nav_term_present(void){if(terminal_active)tb_present();}

void nav_term_text(int x,int y,int width,const char *text,NavStyle style){
    int ended=text==NULL;
    for(int i=0;i<width;i++){
        unsigned char ch=' ';
        if(!ended){ch=(unsigned char)text[i];if(ch==0){ended=1;ch=' ';}}
        put_cell(x+i,y,ch,style);
    }
}

void nav_term_glyph(int x,int y,unsigned char glyph,NavStyle style){put_cell(x,y,tdx_cp437_to_unicode(glyph),style);}
void nav_term_hline(int x,int y,unsigned char glyph,int count,NavStyle style){while(count-->0)nav_term_glyph(x++,y,glyph,style);}
void nav_term_vline(int x,int y,unsigned char glyph,int count,NavStyle style){while(count-->0)nav_term_glyph(x,y++,glyph,style);}
void nav_term_cursor(int x,int y){if(terminal_active)tb_set_cursor(x,y);}
void nav_term_hide_cursor(void){if(terminal_active)tb_hide_cursor();}

static int translate_key(unsigned key){
    switch(key){
        case TB_KEY_F1:return NAV_KEY_F1; case TB_KEY_F2:return NAV_KEY_F2;
        case TB_KEY_F3:return NAV_KEY_F3; case TB_KEY_F4:return NAV_KEY_F4;
        case TB_KEY_F5:return NAV_KEY_F5; case TB_KEY_F6:return NAV_KEY_F6;
        case TB_KEY_F7:return NAV_KEY_F7; case TB_KEY_F8:return NAV_KEY_F8;
        case TB_KEY_F9:return NAV_KEY_F9; case TB_KEY_F10:return NAV_KEY_F10;
        case TB_KEY_F11:return NAV_KEY_F11; case TB_KEY_F12:return NAV_KEY_F12;
        case TB_KEY_ARROW_UP:return NAV_KEY_UP; case TB_KEY_ARROW_DOWN:return NAV_KEY_DOWN;
        case TB_KEY_ARROW_LEFT:return NAV_KEY_LEFT; case TB_KEY_ARROW_RIGHT:return NAV_KEY_RIGHT;
        case TB_KEY_HOME:return NAV_KEY_HOME; case TB_KEY_END:return NAV_KEY_END;
        case TB_KEY_PGUP:return NAV_KEY_PAGE_UP; case TB_KEY_PGDN:return NAV_KEY_PAGE_DOWN;
        case TB_KEY_DELETE:return NAV_KEY_DELETE;
        case TB_KEY_ENTER:return NAV_KEY_ENTER; case TB_KEY_TAB:return NAV_KEY_TAB;
        case TB_KEY_BACKSPACE: case TB_KEY_BACKSPACE2:return NAV_KEY_BACKSPACE;
        case TB_KEY_ESC:return NAV_KEY_ESCAPE;
        case TB_KEY_CTRL_BACKSLASH:return '\\';
        default:return key<128?(int)key:NAV_KEY_NONE;
    }
}

int nav_term_poll_event(NavTermEvent *event,int timeout_ms){
    struct tb_event raw;
    int result;
    if(!event)return -1;
    memset(event,0,sizeof *event);
    result=timeout_ms<0?tb_poll_event(&raw):tb_peek_event(&raw,timeout_ms);
    if(result<0)return result;
    if(raw.type==TB_EVENT_RESIZE){
        event->type=NAV_TERM_EVENT_RESIZE;
        event->width=raw.w;
        event->height=raw.h;
        tb_invalidate();
        return 1;
    }
    if(raw.type!=TB_EVENT_KEY)return 1;
    event->type=NAV_TERM_EVENT_KEY;
    event->key=raw.ch?(int)raw.ch:translate_key(raw.key);
    if(raw.mod&TB_MOD_ALT)event->modifiers|=NAV_MOD_ALT;
    if(raw.mod&TB_MOD_CTRL)event->modifiers|=NAV_MOD_CTRL;
    if(raw.mod&TB_MOD_SHIFT)event->modifiers|=NAV_MOD_SHIFT;
    if(raw.key==TB_KEY_CTRL_BACKSLASH)event->modifiers|=NAV_MOD_CTRL;
    return 1;
}
