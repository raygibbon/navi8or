#ifndef NAV_H
#define NAV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define NAV_PATH_MAX 4096
#define NAV_NAME_MAX 256
enum { NAV_CAP_LIST = 1u, NAV_CAP_READ = 2u, NAV_CAP_WRITE = 4u,
       NAV_CAP_DELETE = 8u, NAV_CAP_MKDIR = 16u };
enum { NAV_ENTRY_DIR = 1u, NAV_ENTRY_PARENT = 2u };
typedef enum { NAV_SORT_NAME, NAV_SORT_SIZE, NAV_SORT_DATE } NavSortMode;
typedef void (*NavProgressFn)(uint64_t done, uint64_t total, void *userdata);

typedef struct { char name[NAV_NAME_MAX]; char path[NAV_PATH_MAX]; uint64_t size;
                 time_t modified; unsigned flags; } NavEntry;
typedef struct { NavEntry *items; size_t count, capacity; } NavListing;
typedef struct NavProvider NavProvider;
struct NavProvider {
    const char *scheme; unsigned capabilities;
    int (*list)(NavProvider *, const char *, bool, NavListing *, char *, size_t);
    int (*rename_path)(NavProvider *, const char *, const char *, char *, size_t);
    int (*remove)(NavProvider *, const char *, char *, size_t);
    int (*mkdir)(NavProvider *, const char *, char *, size_t);
    int (*stat)(NavProvider *, const char *, NavEntry *, char *, size_t);
    int (*open_read)(NavProvider *, const char *, void **, char *, size_t);
    int (*open_write)(NavProvider *, const char *, bool, void **, char *, size_t);
    int (*read)(NavProvider *, void *, void *, size_t, size_t *, char *, size_t);
    int (*write)(NavProvider *, void *, const void *, size_t, char *, size_t);
    int (*close)(NavProvider *, void *, char *, size_t);
};
typedef struct { char paths[64][NAV_PATH_MAX]; int count, current; } NavHistory;
typedef struct { NavProvider *provider; char path[NAV_PATH_MAX]; NavListing listing;
                 NavHistory history; NavSortMode sort_mode; int selected, offset;
                 char filter[NAV_NAME_MAX]; } NavPane;
typedef struct { uint64_t total, transferred; double bytes_per_second; int state; } NavTransfer;
typedef struct { NavPane panes[2]; int active; bool show_hidden, running;
                 char status[256]; } NavApp;
typedef struct { const char *executable; const char *const *arguments;
                 size_t argument_count; bool wait; } NavEditorConfig;

NavProvider *nav_local_provider(void);
void nav_listing_free(NavListing *);
int nav_path_normalize(const char *, char *, size_t);
int nav_path_join(const char *, const char *, char *, size_t);
int nav_path_parent(const char *, char *, size_t);
const char *nav_path_basename(const char *);
bool nav_path_is_absolute(const char *);
void nav_history_push(NavHistory *, const char *);
const char *nav_history_back(NavHistory *);
const char *nav_history_forward(NavHistory *);
int nav_pane_load(NavPane *, const char *, bool, bool, char *, size_t);
int nav_pane_visible_count(const NavPane *);
const NavEntry *nav_pane_selected(const NavPane *);
void nav_pane_clamp_selection(NavPane *);
void nav_pane_ensure_visible(NavPane *, int rows);
void nav_pane_sort(NavPane *, NavSortMode);
int nav_transfer_copy(NavProvider *, const char *, NavProvider *, const char *,
                      bool overwrite, NavProgressFn, void *, char *, size_t);
const NavEditorConfig *nav_editor_default_config(void);
int nav_platform_launch_editor(const NavEditorConfig *,const char *,char *,size_t);

int nav_ui_run(NavApp *);
#endif
