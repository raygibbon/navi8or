#ifndef NAV_UI_H
#define NAV_UI_H
#include "nav.h"
#include "nav_terminal.h"
typedef void (*NavUiRedrawFn)(void *userdata);
void nav_ui_text(int,int,int,const char *,NavStyle);
void nav_ui_box(int,int,int,int,const char *,NavStyle);
int nav_prompt_text(const char *,const char *,char *,size_t,NavUiRedrawFn,void *);
bool nav_confirm(const char *,NavUiRedrawFn,void *);
void nav_info(const char *,const char *const *,size_t);
void nav_view_file(const NavEntry *);
#endif
