#ifndef NAV_UI_CORE_H
#define NAV_UI_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include "nav_ui.h"

typedef struct
{
    int x, y, width, height;
    NavTermCell *cells;
} NavUiArea;

typedef struct
{
    bool output_space;
    bool shadow;
    int shadow_width;
} NavUiOutputContext;

typedef struct
{
    int col, row, width, height;
    int shadow_width;
} NavUiAreaGeometry;

NavUiOutputContext nav_ui_output_context(bool);
void nav_ui_adjust_area(NavUiAreaGeometry *, int, int, int, int, int, int,
                       const NavUiOutputContext *);
int nav_ui_save_area(NavUiArea *, int, int, int, int,
                    const NavUiOutputContext *);
void nav_ui_restore_area(NavUiArea *);
void nav_ui_free_area(NavUiArea *);
void nav_ui_shadow_area(int, int, int, int, const NavUiOutputContext *);
void nav_ui_s_output(const char *, int, int, NavStyle,
                    const NavUiOutputContext *);
char *nav_ui_create_frame_rows(int, int, const int *, size_t);
void nav_ui_frame(int x, int y, int width, int height, const int *, size_t,
                 const char *title, NavStyle style);
void nav_ui_window_header(int, int, int, int, char, const char *, const char *, bool);
void nav_ui_vertical_separator(int, int, int);
void nav_ui_mode_line(const char *);

/*
 * Adapted from the TDX/TDE MENU_STR/MINOR_STR model. Navi8or owns this
 * implementation and API; its control state remains independent of commands.
 */
typedef struct NavUiMenu NavUiMenu;
typedef struct
{
    int column;
    int width;
} NavUiMajor;
typedef struct
{
    const char *line;
    int command;
    const NavUiMenu *popout;
    bool disabled;
    bool separator;
    char accelerator;
} NavUiMenuItem;

struct NavUiMenu
{
    const char *label;
    const NavUiMenuItem *minor;
    size_t minor_count;
    size_t current;
};

void nav_ui_get_bar_spacing(const NavUiMenu *, size_t, int, NavUiMajor *);
void nav_ui_get_bar_spacing_for_style(const NavUiMenu *, size_t, int,
                                      NavUiStyle, NavUiMajor *);
void nav_ui_draw_lite_head(const NavUiMenu *, size_t, const NavUiMajor *, size_t);
void nav_ui_draw_menu_bar(const NavUiMenu *, size_t);

enum
{
    NAV_UI_MENU_CANCELLED = -1,
    NAV_UI_MENU_RESIZED = -2
};
size_t nav_ui_menu_move_minor(const NavUiMenu *, size_t, int);
size_t nav_ui_menu_move_major(size_t, size_t, int);
int nav_ui_menu_major_motion(const NavAction *);
int nav_ui_menu_accelerator(const NavUiMenu *, int, size_t *);
int nav_ui_menu_activate(const NavUiMenu *, size_t);
int nav_ui_pull_down(NavUiMenu *, size_t, int *, NavUiRedrawFn, void *);

/*
 * Adapted from TDX/TDE query.c field editing. Navi8or owns this implementation
 * and API while retaining the original cursor and deletion semantics.
 */
typedef struct
{
    char *buffer;
    size_t capacity;
    size_t length;
    size_t cursor;
    size_t offset;
} NavUiField;

typedef enum
{
    NAV_UI_FIELD_IGNORED,
    NAV_UI_FIELD_MOVED,
    NAV_UI_FIELD_CHANGED,
    NAV_UI_FIELD_ACCEPTED,
    NAV_UI_FIELD_CANCELLED
} NavUiFieldResult;

void nav_ui_field_init(NavUiField *, char *, size_t);
void nav_ui_field_ensure_visible(NavUiField *, size_t);
NavUiFieldResult nav_ui_field_event(NavUiField *, const NavAction *);
int nav_ui_prompt_text(const char *, const char *, char *, size_t,
                       NavUiRedrawFn, void *);
bool nav_ui_confirm(const char *, NavUiRedrawFn, void *);

typedef struct
{
    size_t top;
} NavUiTextView;
void nav_ui_text_view_clamp(NavUiTextView *, size_t, size_t);
void nav_ui_text_view_command(NavUiTextView *, NavCommand, size_t, size_t);
void nav_ui_info(const char *, const char *const *, size_t);

#endif
