#ifndef TDXUI_H
#define TDXUI_H

#include <stdbool.h>
#include <stddef.h>
#include "nav_ui.h"

/* Adapted from TDX MENU_STR/MINOR_STR: control state is independent of NAV commands. */
typedef struct TdxUiMenu TdxUiMenu;
typedef struct {
    const char *line;
    int command;
    const TdxUiMenu *popout;
    bool disabled;
    bool separator;
    char accelerator;
} TdxUiMenuItem;

struct TdxUiMenu {
    const char *label;
    const TdxUiMenuItem *minor;
    size_t minor_count;
    size_t current;
};

enum { TDXUI_MENU_CANCELLED=-1, TDXUI_MENU_RESIZED=-2 };
size_t tdxui_menu_move_minor(const TdxUiMenu *,size_t,int);
size_t tdxui_menu_move_major(size_t,size_t,int);
int tdxui_menu_major_motion(const NavTermEvent *);
int tdxui_menu_accelerator(const TdxUiMenu *,int,size_t *);
int tdxui_pull_down(TdxUiMenu *,size_t,int *,NavUiRedrawFn,void *);

/* Adapted from TDX query.c edit-field cursor and deletion semantics. */
typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    size_t cursor;
    size_t offset;
} TdxUiField;

typedef enum {
    TDXUI_FIELD_IGNORED,
    TDXUI_FIELD_MOVED,
    TDXUI_FIELD_CHANGED,
    TDXUI_FIELD_ACCEPTED,
    TDXUI_FIELD_CANCELLED
} TdxUiFieldResult;

void tdxui_field_init(TdxUiField *,char *,size_t);
void tdxui_field_ensure_visible(TdxUiField *,size_t);
TdxUiFieldResult tdxui_field_event(TdxUiField *,const NavTermEvent *);

typedef struct { size_t top; } TdxUiTextView;
void tdxui_text_view_clamp(TdxUiTextView *,size_t,size_t);
void tdxui_text_view_key(TdxUiTextView *,int,size_t,size_t);
void tdxui_info(const char *,const char *const *,size_t,NavUiRedrawFn,void *);

#endif
