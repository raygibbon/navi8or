/* Adapted from TDX's termbox2 foundation: NAV keeps terminal events and
 * CP437 conversion at this boundary, not in commander code. */
#define TB_IMPL
#include "termbox2/termbox2.h"
#include "cp437.h"
#include "nav_terminal.h"
#include <locale.h>

static int attr = 1;
static const uintattr_t colors[16]={16,19,34,37,124,127,130,145,240,21,46,51,196,201,226,231};
void nav_term_init(void){setlocale(LC_CTYPE,"");if(tb_init()<0) return;tb_set_output_mode(TB_OUTPUT_256);tb_set_input_mode(TB_INPUT_ALT);}
void nav_term_shutdown(void){tb_shutdown();}
void nav_term_clear(void){tb_clear();}
void nav_term_refresh(void){tb_present();}
void nav_term_get_size(int *r,int *c){*r=tb_height();*c=tb_width();}
void nav_term_attr(int a){attr=a;}
static void put(int y,int x,uint32_t ch){int fg=(attr==2||attr==5)?14:7,bg=(attr==4)?3:1;if(attr==3)fg=3,bg=0;tb_set_cell(x,y,ch,colors[fg],colors[bg]);}
void nav_term_line(int y,int x,int n,const char*s){for(int i=0;i<n;i++)put(y,x+i,s[i]?(unsigned char)s[i]:' ');}
void nav_term_ch(int y,int x,unsigned char c){put(y,x,tdx_cp437_to_unicode(c));}
void nav_term_hline(int y,int x,unsigned char c,int n){while(n--)nav_term_ch(y,x++,c);}
void nav_term_cursor(int y,int x){tb_set_cursor(x,y);}
int nav_term_getkey(void){struct tb_event e;for(;;){if(tb_poll_event(&e)<0)return KEY_F(10);if(e.type==TB_EVENT_RESIZE)return 0;if(e.type!=TB_EVENT_KEY)continue;if(e.mod&TB_MOD_ALT){if(e.key==TB_KEY_ARROW_LEFT)return KEY_ALT_LEFT;if(e.key==TB_KEY_ARROW_RIGHT)return KEY_ALT_RIGHT;if(e.key==TB_KEY_ARROW_UP)return KEY_ALT_UP;}switch(e.key){case TB_KEY_F1:return KEY_F(1);case TB_KEY_F3:return KEY_F(3);case TB_KEY_F4:return KEY_F(4);case TB_KEY_F5:return KEY_F(5);case TB_KEY_F6:return KEY_F(6);case TB_KEY_F7:return KEY_F(7);case TB_KEY_F8:return KEY_F(8);case TB_KEY_F9:return KEY_F(9);case TB_KEY_F10:return KEY_F(10);case TB_KEY_ARROW_UP:return KEY_UP;case TB_KEY_ARROW_DOWN:return KEY_DOWN;case TB_KEY_ARROW_LEFT:return KEY_LEFT;case TB_KEY_ARROW_RIGHT:return KEY_RIGHT;case TB_KEY_HOME:return KEY_HOME;case TB_KEY_END:return KEY_END;case TB_KEY_PGUP:return KEY_PPAGE;case TB_KEY_PGDN:return KEY_NPAGE;case TB_KEY_ENTER:return KEY_ENTER;case TB_KEY_TAB:return '\t';case TB_KEY_BACKSPACE:case TB_KEY_BACKSPACE2:return KEY_BACKSPACE;case TB_KEY_ESC:return 27;default:break;}if(e.ch)return (int)e.ch;if(e.key<128)return (int)e.key;}}
