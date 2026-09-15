#ifndef NAV_PROFILE_H
#define NAV_PROFILE_H
#include "nav_ui.h"

typedef struct {
    NavConfig *before; bool dirty_before, hidden;
    char status[256]; NavMessageKind status_kind;
    NavPanelView views[2]; NavSortMode sorts[2]; bool directories_first[2], case_sensitive[2];
} NavProfileSession;
void nav_profile_mark_saved(NavApp *);
void nav_profile_changed(NavApp *);
int nav_profile_begin(NavApp *, NavProfileSession *);
void nav_profile_cancel(NavApp *, NavProfileSession *);
void nav_profile_commit(NavProfileSession *);
bool nav_profile_same_ui(const NavConfig *, const NavConfig *);
bool nav_settings_same(const NavConfig *, const NavConfig *);
bool nav_settings_dirty(const NavApp *);
void nav_settings_mark_saved(NavApp *);
bool nav_profile_is_template(const char *);
int nav_profile_user_path(const char *, char *, size_t, char *, size_t);
int nav_profile_save(NavConfig *, const char *, char *, size_t);
void nav_ui_profile_apply(NavApp *);
void nav_ui_profile_restore_panes(NavApp *, const NavProfileSession *);
void nav_ui_preferences(NavApp *, NavUiRedrawFn, void *);
bool nav_ui_profile_save(NavApp *, bool, NavUiRedrawFn, void *);
#endif
