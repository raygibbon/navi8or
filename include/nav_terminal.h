#ifndef NAV_TERMINAL_H
#define NAV_TERMINAL_H

#include <stdint.h>

struct NavTheme;

typedef enum
{
    NAV_STYLE_BACKGROUND,
    NAV_STYLE_SURFACE,
    NAV_STYLE_SURFACE_ALT,
    NAV_STYLE_TEXT,
    NAV_STYLE_TEXT_DIM,
    NAV_STYLE_ACCENT,
    NAV_STYLE_BORDER,
    NAV_STYLE_BORDER_ACTIVE,
    NAV_STYLE_SELECTION,
    NAV_STYLE_SELECTION_INACTIVE,
    NAV_STYLE_HEADER,
    NAV_STYLE_STATUS,
    NAV_STYLE_MESSAGE,
    NAV_STYLE_ERROR,
    NAV_STYLE_WARNING,
    NAV_STYLE_DIALOG,
    NAV_STYLE_DIALOG_TITLE,
    NAV_STYLE_MENU,
    NAV_STYLE_MENU_SELECTED,
    NAV_STYLE_MENU_DISABLED,
    NAV_STYLE_MENU_SELECTED_DISABLED,
    NAV_STYLE_KEYBAR,
    NAV_STYLE_KEYBAR_SELECTED,
    NAV_STYLE_KEYBAR_KEY,
    NAV_STYLE_KEYBAR_DISABLED,
    NAV_STYLE_PATH,
    NAV_STYLE_COLUMN_HEADER,
    NAV_STYLE_VIEWER_LINE_NUMBER,
    NAV_STYLE_VIEWER_SEARCH_MATCH,
    NAV_STYLE_PROGRESS,
    NAV_STYLE_PANE_TITLE,
    NAV_STYLE_PANE_TITLE_ACTIVE,
    NAV_STYLE_COUNT
} NavStyle;

typedef struct
{
    uint32_t ch;
    uint32_t foreground;
    uint32_t background;
} NavTermCell;

typedef enum
{
    NAV_KEY_NONE,
    NAV_KEY_UP = 0x110000,
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
    NAV_KEY_F1 = 0x110100,
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

typedef enum
{
    NAV_TERM_EVENT_NONE,
    NAV_TERM_EVENT_KEY,
    NAV_TERM_EVENT_RESIZE
} NavTermEventType;
enum
{
    NAV_MOD_ALT = 1u,
    NAV_MOD_CTRL = 2u,
    NAV_MOD_SHIFT = 4u
};
typedef struct
{
    NavTermEventType type;
    int key;
    unsigned modifiers;
    int width;
    int height;
} NavTermEvent;

void nav_term_set_theme(const struct NavTheme *);
const struct NavTheme *nav_term_theme(void);
int nav_term_init(void);
void nav_term_shutdown(void);
int nav_term_width(void);
int nav_term_height(void);
void nav_term_clear(NavStyle);
void nav_term_present(void);
void nav_term_text(int, int, int, const char *, NavStyle);
void nav_term_glyph(int, int, uint32_t, NavStyle);
void nav_term_hline(int, int, uint32_t, int, NavStyle);
void nav_term_vline(int, int, uint32_t, int, NavStyle);
void nav_term_cursor(int, int);
void nav_term_hide_cursor(void);
int nav_term_get_cell(int, int, NavTermCell *);
int nav_term_set_cell(int, int, const NavTermCell *);
int nav_term_poll_event(NavTermEvent *, int timeout_ms);

#endif
