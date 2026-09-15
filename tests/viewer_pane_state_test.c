#include "nav_ui.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void redraw(void *data) { (void)data; }
int main(void)
{
    NavApp *app = calloc(1, sizeof *app); assert(app);
    NavPane *before = calloc(2, sizeof *before); assert(before);
    nav_config_defaults(&app->config);
    char path[] = "/tmp/nav-pane-state-XXXXXX", error[256];
    int fd = mkstemp(path); assert(fd >= 0);
    FILE *file = fdopen(fd, "w"); assert(file);
    fputs("first\nsecond\nthird\n", file); assert(!fclose(file));
    NavEntry entry;
    for (int i = 0; i < 2; i++) {
        NavPane *pane = &app->panes[i]; pane->provider = nav_local_provider();
        assert(!pane->provider->location(pane->provider, "/tmp", &pane->location, error, sizeof error));
        pane->selected = 7; pane->offset = 3; pane->sort_mode = NAV_SORT_DATE;
        strcpy(pane->filter, "retained filter"); pane->history.current = -1;
        before[i] = *pane;
    }
    assert(!nav_local_provider()->stat(nav_local_provider(), path, &entry, error, sizeof error));
    for (int cycle = 0; cycle < 40; cycle++) {
        for (int i = 0; i < 2; i++) {
            app->active = i;
            assert(nav_ui_viewer_open(app, nav_local_provider(), &entry, false, redraw, app) == 1);
            assert(app->panes[i].viewer && app->panes[i].content_mode == NAV_PANE_VIEWER);
            assert(app->mode == NAV_MODE_COMMANDER && nav_ui_active_context(app) == NAV_CONTEXT_VIEWER);
            nav_ui_viewer_dispatch(app, NAV_CMD_DOWN);
            nav_ui_viewer_dispatch(app, NAV_CMD_VIEWER_FULLSCREEN);
            assert(nav_ui_viewer_fullscreen(&app->panes[i]));
            nav_ui_viewer_dispatch(app, NAV_CMD_VIEWER_FULLSCREEN);
            assert(!nav_ui_viewer_fullscreen(&app->panes[i]));
            assert(!memcmp(&app->panes[i], &before[i], offsetof(NavPane, content_mode)));
        }
        assert(app->panes[0].viewer != app->panes[1].viewer);
        app->active = cycle % 2;
        nav_ui_viewer_dispatch(app, NAV_CMD_VIEWER_CLOSE);
        assert(nav_ui_active_context(app) == NAV_CONTEXT_PANEL);
        assert(app->panes[!app->active].viewer);
        nav_ui_viewer_close(&app->panes[!app->active]);
        for (int i = 0; i < 2; i++) {
            nav_ui_viewer_close(&app->panes[i]); /* idempotent teardown */
            assert(!memcmp(&app->panes[i], &before[i], sizeof *before));
        }
    }
    unlink(path); free(before); free(app);
    puts("Pane Viewer ownership, non-blocking open, file-state retention and repeated independent teardown: passed");
    return 0;
}
