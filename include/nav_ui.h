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
int nav_ui_location_prompt(char *, size_t, NavUiRedrawFn, void *);
int nav_ui_download(NavProvider *, const NavEntry *, const NavLocation *, const NavConfig *, NavUiRedrawFn, void *);
int nav_view_file_from(NavProvider *, const NavEntry *, const NavConfig *, const NavLocation *);
int nav_view_file(NavProvider *, const NavEntry *, const NavConfig *);
void nav_ui_request_quit(void);
bool nav_ui_quit_requested(void);
void nav_ui_binding_help(NavInputContext);
void nav_ui_input_configure(const NavConfig *);
const NavKeymap *nav_ui_keymap(void);
bool nav_ui_show_menu_keys(void);
bool nav_ui_show_app_identity(void);
bool nav_ui_show_dialog_keys(void);
bool nav_ui_show_help_keys(void);
int nav_ui_hint_key(NavInputContext, NavCommand, bool, char *, size_t);
void nav_ui_hints(NavInputContext, const NavCommand *, const char *const *, size_t, char *, size_t);
NavInputContext nav_ui_workspace(NavInputContext);
NavInputContext nav_ui_workspace_context(void);
int nav_ui_input(NavInputContext, NavAction *);
void nav_ui_command_bar(int, int, NavInputContext, bool (*)(void *, NavCommand), void *);
#endif
