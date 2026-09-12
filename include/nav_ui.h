#ifndef NAV_UI_H
#define NAV_UI_H
#include "nav.h"
#include "nav_terminal.h"
typedef void (*NavUiRedrawFn)(void *userdata);
void nav_ui_text(int, int, int, const char *, NavStyle);
void nav_ui_box(int, int, int, int, const char *, NavStyle);
int nav_ui_prompt_text(const char *, const char *, char *, size_t, NavUiRedrawFn, void *);
int nav_ui_prompt_secret(const char *, const char *, char *, size_t, NavUiRedrawFn, void *);
bool nav_ui_confirm(const char *, NavUiRedrawFn, void *);
void nav_ui_info(const char *, const char *const *, size_t);
void nav_show_properties(const NavEntry *, const char *);
int nav_view_file(NavProvider *, const NavEntry *, const NavConfig *);
#endif
