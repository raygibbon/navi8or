#ifndef NAV_TERMINAL_H
#define NAV_TERMINAL_H

#include <stdint.h>

struct NavTheme;

typedef enum {
    NAV_STYLE_NORMAL,
    NAV_STYLE_MENU,
    NAV_STYLE_ACTIVE,
    NAV_STYLE_SELECTED,
    NAV_STYLE_DIALOG,
    NAV_STYLE_VIEW_CURRENT,
    NAV_STYLE_SEARCH_MATCH,
    NAV_STYLE_COUNT
} NavStyle;

typedef enum {
    NAV_KEY_NONE,
    NAV_KEY_UP,
    NAV_KEY_DOWN,
    NAV_KEY_LEFT,
    NAV_KEY_RIGHT,
    NAV_KEY_HOME,
    NAV_KEY_END,
    NAV_KEY_PAGE_UP,
    NAV_KEY_PAGE_DOWN,
    NAV_KEY_DELETE,
    NAV_KEY_BACKSPACE,
    NAV_KEY_ENTER,
    NAV_KEY_ESCAPE,
    NAV_KEY_TAB,
    NAV_KEY_F1 = 1101,
    NAV_KEY_F2,
    NAV_KEY_F3,
    NAV_KEY_F4,
    NAV_KEY_F5,
    NAV_KEY_F6,
    NAV_KEY_F7,
    NAV_KEY_F8,
    NAV_KEY_F9,
    NAV_KEY_F10,
    NAV_KEY_F11,
    NAV_KEY_F12
} NavKey;

typedef enum { NAV_TERM_EVENT_NONE, NAV_TERM_EVENT_KEY, NAV_TERM_EVENT_RESIZE } NavTermEventType;
enum { NAV_MOD_ALT = 1u, NAV_MOD_CTRL = 2u, NAV_MOD_SHIFT = 4u };
typedef struct {
    NavTermEventType type;
    int key;
    unsigned modifiers;
    int width;
    int height;
} NavTermEvent;

void nav_term_set_theme(const struct NavTheme *);
int nav_term_init(void);
void nav_term_shutdown(void);
int nav_term_width(void);
int nav_term_height(void);
void nav_term_clear(NavStyle);
void nav_term_present(void);
void nav_term_text(int,int,int,const char *,NavStyle);
void nav_term_glyph(int,int,unsigned char,NavStyle);
void nav_term_hline(int,int,unsigned char,int,NavStyle);
void nav_term_vline(int,int,unsigned char,int,NavStyle);
void nav_term_cursor(int,int);
void nav_term_hide_cursor(void);
int nav_term_poll_event(NavTermEvent *,int timeout_ms);

#endif
