#ifndef NAV_H
#define NAV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "nav_credential.h"
#include "nav_platform.h"
#include "nav_input.h"

#define NAV_PATH_MAX 4096
#define NAV_NAME_MAX 256
#define NAV_EDITOR_ARG_MAX 16
#define NAV_EDITOR_ARG_MAX_LENGTH 512
#define NAV_HISTORY_MAX 128
#define NAV_REPOSITORY_MAX 32
#define NAV_REPO_NAME_MAX 96
#define NAV_URL_MAX 2048
#define NAV_TRANSFER_BUFFER_MIN (64u * 1024u)
#define NAV_TRANSFER_BUFFER_MAX (64u * 1024u * 1024u)
enum
{
    NAV_CAP_LIST = 1u,
    NAV_CAP_READ = 2u,
    NAV_CAP_WRITE = 4u,
    NAV_CAP_DELETE = 8u,
    NAV_CAP_MKDIR = 16u,
    NAV_CAP_RENAME = 32u,
    NAV_CAP_STAT = 64u,
    NAV_CAP_EDIT_LOCAL = 128u,
    NAV_CAP_RANDOM_READ = 256u
};
enum
{
    NAV_ENTRY_DIR = 1u,
    NAV_ENTRY_PARENT = 2u,
    NAV_ENTRY_SIZE_KNOWN = 4u,
    NAV_ENTRY_MODIFIED_KNOWN = 8u
};
typedef enum
{
    NAV_SORT_NAME,
    NAV_SORT_SIZE,
    NAV_SORT_DATE
} NavSortMode;
typedef enum { NAV_PANEL_BRIEF, NAV_PANEL_FULL } NavPanelView;
typedef enum
{
    NAV_UI_STYLE_MODERN,
    NAV_UI_STYLE_CLASSIC
} NavUiStyle;
typedef enum
{
    NAV_MESSAGE_STATUS,
    NAV_MESSAGE_NOTICE,
    NAV_MESSAGE_WARNING,
    NAV_MESSAGE_ERROR
} NavMessageKind;
typedef struct
{
    char name[NAV_REPO_NAME_MAX];
    char url[NAV_URL_MAX];
    /* Optional reference into the credential store; never contains a secret. */
    char credential[NAV_CREDENTIAL_NAME_MAX];
    bool tls_verify;
    bool writable;
    bool mkdir_enabled;
    bool delete_enabled;
    bool rename_enabled;
} NavRepository;
typedef struct
{
    int name_width;
    int size_column, size_width;
    int modified_column, modified_width;
    bool show_size, show_modified;
} NavPanelFullLayout;
typedef struct
{
    NavKeymap keymap;
    char config_path[NAV_PATH_MAX];
    bool explicit_config;
    bool confirm_delete;
    bool confirm_overwrite;
    bool show_hidden;
    bool directories_first;
    bool case_sensitive_sort;
    NavSortMode sort;
    NavPanelView panel_view;
    bool viewer_line_numbers;
    bool viewer_wrap;
    bool viewer_current_line;
    char editor_command[128];
    char editor_args[NAV_EDITOR_ARG_MAX][NAV_EDITOR_ARG_MAX_LENGTH];
    size_t editor_arg_count;
    bool editor_wait;
    bool menu_remember_position;
    size_t transfer_buffer_size;
    bool history_enabled;
    size_t history_max_entries;
    char theme_name[64];
    char warning[256];
    NavRepository repositories[NAV_REPOSITORY_MAX];
    size_t repository_count;
} NavConfig;
typedef enum
{
    NAV_MODE_COMMANDER,
    NAV_MODE_MENU,
    NAV_MODE_VIEWER,
    NAV_MODE_DIALOG,
    NAV_MODE_PROMPT,
    NAV_MODE_HELP,
    NAV_MODE_TRANSFER,
    NAV_MODE_EDITOR,
    NAV_MODE_TERMINAL
} NavMode;
typedef void (*NavProgressFn)(uint64_t done, uint64_t total, bool total_known, void *userdata);

typedef struct
{
    char name[NAV_NAME_MAX];
    /* Provider-owned opaque identity; it is not necessarily a pathname. */
    char resource_id[NAV_PATH_MAX];
    uint64_t size;
    time_t modified;
    unsigned flags;
} NavEntry;
typedef struct
{
    NavEntry *items;
    size_t count, capacity;
} NavListing;
typedef struct NavProvider NavProvider;
typedef struct
{
    NavProvider *provider;
    char resource_id[NAV_PATH_MAX];
    char display_path[NAV_PATH_MAX];
} NavLocation;
struct NavProvider
{
    const char *scheme;
    const char *display_name;
    unsigned capabilities;
    void *context;
    void (*destroy)(NavProvider *);
    int (*location)(NavProvider *, const char *, NavLocation *, char *, size_t);
    int (*location_child)(NavProvider *, const NavLocation *, const char *, NavLocation *, char *, size_t);
    int (*location_parent)(NavProvider *, const NavLocation *, NavLocation *, char *, size_t);
    int (*list)(NavProvider *, const char *, bool, NavListing *, char *, size_t);
    int (*rename_path)(NavProvider *, const char *, const char *, char *, size_t);
    int (*remove)(NavProvider *, const char *, char *, size_t);
    int (*mkdir)(NavProvider *, const char *, char *, size_t);
    int (*stat)(NavProvider *, const char *, NavEntry *, char *, size_t);
    int (*open_read)(NavProvider *, const char *, void **, char *, size_t);
    int (*open_write)(NavProvider *, const char *, bool, uint64_t, bool,
                      void **, char *, size_t);
    int (*read)(NavProvider *, void *, void *, size_t, size_t *, char *, size_t);
    int (*read_at)(NavProvider *, const char *, uint64_t, void *, size_t,
                   size_t *, bool *, uint64_t *, bool *, char *, size_t);
    int (*write)(NavProvider *, void *, const void *, size_t, char *, size_t);
    /* True once a write handle has supplied body bytes to its transport. */
    bool (*write_started)(NavProvider *, void *);
    int (*close)(NavProvider *, void *, char *, size_t);
};
typedef struct
{
    NavLocation locations[NAV_HISTORY_MAX];
    int count, current, limit;
} NavHistory;
typedef struct
{
    NavProvider *provider;
    NavLocation location;
    NavListing listing;
    NavHistory history;
    NavSortMode sort_mode;
    bool directories_first;
    bool case_sensitive_sort;
    bool history_enabled;
    NavPanelView view;
    int rows_per_column, visible_columns;
    int selected, offset;
    char filter[NAV_NAME_MAX];
} NavPane;
typedef struct
{
    int width, height;
    int divider;
    int pane_x[2], pane_width[2];
    int top_row, menu_row;
    int pane_title_row, pane_location_row, pane_column_header_row;
    int pane_column_separator_row;
    int body_top, body_bottom, body_height;
    int summary_row, status_row, key_bar_row;
} NavCommanderLayout;
typedef struct
{
    int key;
    int x, width, key_width;
    const char *label;
    NavCommand command;
} NavFunctionKeySegment;
typedef struct
{
    uint64_t total, transferred;
    bool total_known;
    double bytes_per_second;
    int state;
} NavTransfer;
typedef struct
{
    NavPane panes[2];
    int active;
    NavMode mode;
    NavMode previous_mode;
    bool show_hidden, running;
    NavConfig config;
    char status[256];
    NavMessageKind status_kind;
    NavCredentialStore *credential_store;
} NavApp;
void nav_config_defaults(NavConfig *);
int nav_config_load_file(NavConfig *, const char *, char *, size_t);
int nav_config_load(NavConfig *, char *, size_t);
int nav_config_validate(const NavConfig *, char *, size_t);
int nav_config_write_defaults(char *, size_t);
int nav_config_save_repositories(const NavConfig *, char *, size_t);
int nav_repository_normalize_url(const char *, char *, size_t, char *, size_t);
typedef struct
{
    const char *executable;
    const char *const *arguments;
    size_t argument_count;
    bool wait;
} NavEditorConfig;

bool nav_provider_supports_credential_type(const char *, NavCredentialType);
NavProvider *nav_local_provider(void);
NavProvider *nav_http_provider_create(const NavRepository *, NavCredentialStore *,
                                      char *, size_t);
NavProvider *nav_smb_provider_create(const NavRepository *, NavCredentialStore *,
                                     char *, size_t);
/* Returns a borrowed current/local provider. Relative locations retain current;
   absolute paths select local. Explicit scheme:// URLs require a matching
   configured provider. Colon filenames are not URIs; Windows drive paths
   select local without changing platform-specific path normalization. */
NavProvider *nav_provider_for_location(NavProvider *, const char *, char *, size_t);
/* Creates a configured provider; release it with nav_provider_destroy. */
NavProvider *nav_provider_create_repository(const NavRepository *,
                                           NavCredentialStore *, char *, size_t);
int nav_http_parse_directory_html(NavProvider *, const char *, const char *,
                                  NavListing *, char *, size_t);
void nav_provider_destroy(NavProvider *);
void nav_listing_free(NavListing *);
int nav_path_normalize(const char *, char *, size_t);
int nav_path_join(const char *, const char *, char *, size_t);
int nav_path_parent(const char *, char *, size_t);
const char *nav_path_basename(const char *);
bool nav_path_is_absolute(const char *);
/* NavEntry names have NAV_NAME_MAX bytes including the terminating NUL. */
bool nav_leaf_name_copy(char[NAV_NAME_MAX], const char *);
void nav_history_push(NavHistory *, const NavLocation *);
const NavLocation *nav_history_back(NavHistory *);
const NavLocation *nav_history_forward(NavHistory *);
int nav_pane_load(NavPane *, const NavLocation *, bool, bool, char *, size_t);
int nav_pane_open(NavPane *, const char *, bool, bool, char *, size_t);
int nav_pane_refresh(NavPane *, bool, char *, size_t);
bool nav_provider_supports(const NavProvider *, unsigned);
bool nav_provider_resources_equal(const NavProvider *, const char *, const NavProvider *, const char *);
NavPane *nav_active_pane(NavApp *);
NavPane *nav_other_pane(NavApp *);
int nav_pane_visible_count(const NavPane *);
const NavEntry *nav_pane_selected(const NavPane *);
void nav_pane_clamp_selection(NavPane *);
void nav_pane_ensure_visible(NavPane *);
int nav_pane_page_capacity(const NavPane *);
void nav_pane_set_layout(NavPane *, int width, int rows);
void nav_pane_move(NavPane *, int delta);
void nav_pane_page(NavPane *, int direction);
void nav_entry_format_display_name(const NavEntry *, char *, size_t);
void nav_format_size(uint64_t, char *, size_t);
void nav_format_panel_header(NavPanelView, NavSortMode, int, char *, size_t);
void nav_format_entry_full(const NavEntry *, int, char *, size_t);
void nav_panel_full_layout(int, NavPanelFullLayout *);
bool nav_commander_layout(int, int, NavCommanderLayout *);
bool nav_commander_layout_for_style(int, int, NavUiStyle,
                                    NavCommanderLayout *);
typedef struct {
    int width, height, menu_row, workspace_top, workspace_bottom, status_row, command_row;
} NavShellLayout;
NavShellLayout nav_shell_layout(int, int);
size_t nav_function_key_layout(int, const NavKeymap *, NavInputContext, NavFunctionKeySegment *, size_t);
void nav_provider_display_name(const NavProvider *, char *, size_t);
bool nav_entry_has_known_size(const NavEntry *);
void nav_pane_sort(NavPane *, NavSortMode);
int nav_transfer_copy(NavProvider *, const char *, NavProvider *, const char *,
                      bool overwrite, NavProgressFn, void *, char *, size_t);
int nav_transfer_copy_with_buffer(NavProvider *, const char *, NavProvider *, const char *,
                                  bool overwrite, size_t buffer_size, NavProgressFn, void *, char *, size_t);
const NavEditorConfig *nav_editor_default_config(void);
int nav_platform_launch_editor(const NavEditorConfig *, const char *, char *, size_t);
int nav_platform_config_dir(char *, size_t);

int nav_ui_run(NavApp *);
#endif
