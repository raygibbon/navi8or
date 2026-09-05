#ifndef NAV_TERMINAL_H
#define NAV_TERMINAL_H

#include <stdint.h>
#define TRUE 1
enum { KEY_UP=1001, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END,
       KEY_PPAGE, KEY_NPAGE, KEY_BACKSPACE, KEY_ENTER, KEY_ALT_LEFT,
       KEY_ALT_RIGHT, KEY_ALT_UP };
#define KEY_F(n) (1100 + (n))
#define ACS_ULCORNER 0xda
#define ACS_URCORNER 0xbf
#define ACS_LLCORNER 0xc0
#define ACS_LRCORNER 0xd9
#define ACS_HLINE 0xc4
#define ACS_VLINE 0xb3
#define COLOR_WHITE 7
#define COLOR_YELLOW 14
#define COLOR_BLUE 1
#define COLOR_CYAN 3
#define COLOR_BLACK 0
#define COLOR_PAIR(n) (n)
void nav_term_init(void); void nav_term_shutdown(void); void nav_term_clear(void);
void nav_term_refresh(void); void nav_term_get_size(int *,int *); void nav_term_attr(int);
void nav_term_line(int,int,int,const char *); void nav_term_ch(int,int,unsigned char);
void nav_term_hline(int,int,unsigned char,int); void nav_term_cursor(int,int); int nav_term_getkey(void);
#define initscr() nav_term_init()
#define endwin() nav_term_shutdown()
#define erase() nav_term_clear()
#define refresh() nav_term_refresh()
#define getmaxyx(win,rows,cols) nav_term_get_size(&(rows),&(cols))
#define stdscr 0
#define attrset(a) nav_term_attr(a)
#define mvaddnstr(y,x,s,n) nav_term_line((y),(x),(n),(s))
#define mvaddch(y,x,c) nav_term_ch((y),(x),(unsigned char)(c))
#define mvhline(y,x,c,n) nav_term_hline((y),(x),(unsigned char)(c),(n))
#define move(y,x) nav_term_cursor((y),(x))
#define getch() nav_term_getkey()
#define cbreak() ((void)0)
#define noecho() ((void)0)
#define keypad(w,b) ((void)0)
#define curs_set(v) ((void)0)
#define start_color() ((void)0)
#define use_default_colors() ((void)0)
#define init_pair(a,b,c) ((void)0)
#endif
