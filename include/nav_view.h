#ifndef NAV_VIEW_H
#define NAV_VIEW_H

#include <stdbool.h>
#include <stddef.h>

#define NAV_SEARCH_MAX 256

typedef struct NavViewSource NavViewSource;
struct NavViewSource {
    size_t (*line_count)(NavViewSource *);
    const char *(*line)(NavViewSource *,size_t,size_t *);
    size_t (*line_length)(NavViewSource *,size_t);
    void (*close)(NavViewSource *);
    void *implementation;
};

NavViewSource *nav_view_source_open_local(const char *,bool *,char *,size_t);

typedef struct {
    NavViewSource *source;
    size_t top_line;
    size_t current_line;
    size_t horizontal_offset;
    bool wrap;
    bool line_numbers;
    char search[NAV_SEARCH_MAX];
    size_t match_line;
    size_t match_column;
    size_t match_length;
    char status[128];
} NavViewer;

void nav_viewer_init(NavViewer *,NavViewSource *);
void nav_viewer_ensure_visible(NavViewer *,size_t);
void nav_viewer_move(NavViewer *,long,size_t);
void nav_viewer_page(NavViewer *,long,size_t);
void nav_viewer_top(NavViewer *);
void nav_viewer_bottom(NavViewer *,size_t);
void nav_viewer_horizontal(NavViewer *,long);
size_t nav_viewer_line_number_width(const NavViewer *);
void nav_viewer_goto_line(NavViewer *,size_t,size_t);
bool nav_viewer_find(NavViewer *,int,size_t,bool *);

#endif
