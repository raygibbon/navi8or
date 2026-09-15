#include "nav_ui.h"
#include "nav_ui_core.h"
#include <stdio.h>
#include <string.h>

/* Shared editable fields and command-driven buttons, including narrow screens. */
static int location_form(const char *title, const char *source,
                         const char *const *labels, char **values, const size_t *sizes,
                         int count, bool choose_action, NavUiRedrawFn redraw, void *data)
{
    NavUiField fields[3];
    for (int i = 0; i < count; i++) nav_ui_field_init(&fields[i], values[i], sizes[i]);
    int focus = 0, button = 0;
    for (;;) {
        if (focus >= count || (!fields[focus].paste && !fields[focus].paste_failed)) {
            if (redraw) redraw(data); else nav_term_clear(NAV_STYLE_BACKGROUND);
            int width = nav_term_width(), height = nav_term_height();
            int w = width > 90 ? 90 : width - 2, h = count + (source ? 7 : 5);
            int x = (width - w) / 2, y = (height - h) / 2;
            if (w < 32 || height < h) {
                nav_ui_text(0, 0, width, "Resize terminal to edit location", NAV_STYLE_MESSAGE);
            } else {
                nav_ui_box(x, y, w, h, title, NAV_STYLE_DIALOG);
                int top = y + 2;
                if (source) { nav_ui_text(x + 2, top++, w - 4, source, NAV_STYLE_DIALOG); top++; }
                for (int i = 0; i < count; i++) {
                    int label = (int)strlen(labels[i]);
                    nav_ui_text(x + 2, top + i, label, labels[i], NAV_STYLE_DIALOG);
                    int available = w - label - 4;
                    nav_ui_field_draw(&fields[i], x + 2 + label, top + i, available,
                                      NAV_STYLE_TEXT, false, focus == i);
                }
                const char *buttons[] = {choose_action ? "Open" : "Download", choose_action ? "Download" : "Cancel", "Cancel"};
                int n = choose_action ? 3 : 2;
                for (int i = 0; i < n; i++) nav_ui_text(x + 3 + i * (w - 6) / n, y + h - 2,
                    (w - 6) / n, buttons[i], focus == count && button == i ? NAV_STYLE_SELECTION : NAV_STYLE_DIALOG);
                if (focus == count) nav_term_hide_cursor();
            }
            nav_term_present();
        }
        NavAction action;
        if (nav_ui_input(NAV_CONTEXT_DIALOG, &action) <= 0) continue;
        if (action.type == NAV_TERM_EVENT_PASTE_START || action.type == NAV_TERM_EVENT_PASTE_END) {
            if (focus < count && nav_ui_field_event(&fields[focus], &action) == NAV_UI_FIELD_ERROR) {
                const char *lines[] = {fields[focus].error}; nav_ui_info(" Text Entry ", lines, 1);
            }
            continue;
        }
        if (action.type != NAV_TERM_EVENT_KEY) continue;
        if (action.command == NAV_CMD_CANCEL) { for (int i = 0; i < count; i++) nav_ui_field_destroy(&fields[i]); nav_term_hide_cursor(); return -1; }
        if (action.command == NAV_CMD_DOWN) { focus = (focus + 1) % (count + 1); continue; }
        if (action.command == NAV_CMD_UP) { focus = (focus + count) % (count + 1); continue; }
        if (focus == count && (action.command == NAV_CMD_LEFT || action.command == NAV_CMD_RIGHT)) {
            int n = choose_action ? 3 : 2; button = (button + (action.command == NAV_CMD_RIGHT ? 1 : n - 1)) % n; continue;
        }
        if (action.command == NAV_CMD_ACCEPT) {
            if (focus < count && count > 1) { focus++; continue; }
            nav_term_hide_cursor();
            for (int i = 0; i < count; i++) nav_ui_field_destroy(&fields[i]);
            return button == (choose_action ? 2 : 1) ? -1 : button;
        }
        if (focus < count && nav_ui_field_event(&fields[focus], &action) == NAV_UI_FIELD_ERROR) {
            const char *lines[] = {fields[focus].error}; nav_ui_info(" Text Entry ", lines, 1);
        }
    }
}

int nav_ui_location_prompt(char *value, size_t size, NavUiRedrawFn redraw, void *data)
{
    const char *labels[] = {"Location: "}; char *values[] = {value}; size_t sizes[] = {size};
    return location_form(" Enter URL / Location ", NULL, labels, values, sizes, 1, true, redraw, data);
}

typedef struct { NavUiRedrawFn redraw; void *data; const char *name; NavInput input; } DownloadScreen;
static void download_progress(uint64_t done, uint64_t total, bool known, void *data)
{
    DownloadScreen *screen = data;
    if (screen->redraw) screen->redraw(screen->data);
    int w = nav_term_width() > 70 ? 70 : nav_term_width() - 2;
    int x = (nav_term_width() - w) / 2, y = nav_term_height() / 2 - 3;
    if (w > 10 && nav_term_height() > 8) {
        char text[192], hint[128];
        nav_ui_box(x, y, w, 7, " Downloading ", NAV_STYLE_DIALOG);
        nav_ui_text(x + 2, y + 2, w - 4, screen->name, NAV_STYLE_DIALOG);
        if (known) snprintf(text, sizeof text, "%llu / %llu bytes (%u%%)", (unsigned long long)done, (unsigned long long)total, total ? (unsigned)(100.0 * (double)done / (double)total) : 100u);
        else snprintf(text, sizeof text, "%llu bytes", (unsigned long long)done);
        nav_ui_text(x + 2, y + 3, w - 4, text, NAV_STYLE_DIALOG);
        NavCommand cmd = NAV_CMD_CANCEL; nav_ui_hints(NAV_CONTEXT_DIALOG, &cmd, NULL, 1, hint, sizeof hint);
        nav_ui_text(x + 2, y + 5, w - 4, hint, NAV_STYLE_DIALOG);
    }
    nav_term_present();
}
static bool download_cancel(void *data)
{
    DownloadScreen *screen = data;
    NavTermEvent event;
    if (nav_term_poll_event(&event, 0) <= 0) return false;
    return nav_input_resolve(&screen->input, nav_ui_keymap(), NAV_CONTEXT_DIALOG, &event).command == NAV_CMD_CANCEL;
}

int nav_ui_download(NavProvider *source, const NavEntry *entry,
                    const NavLocation *origin, const NavConfig *config,
                    NavUiRedrawFn redraw, void *data)
{
    char directory[NAV_PATH_MAX], name[NAV_NAME_MAX], error[256] = {0}, line[NAV_PATH_MAX + 16];
    snprintf(directory, sizeof directory, "%s", origin->resource_id);
    snprintf(name, sizeof name, "%s", entry->name);
    snprintf(line, sizeof line, "Source: %s", entry->resource_id);
    const char *labels[] = {"Destination: ", "Filename: "}; char *values[] = {directory, name};
    size_t sizes[] = {sizeof directory, sizeof name};
    if (location_form(" Download / Save Copy ", line, labels, values, sizes, 2, false, redraw, data)) return 0;
    char leaf[NAV_NAME_MAX]; NavLocation parent, target;
    NavProvider *destination = nav_provider_for_location(origin->provider, directory, error, sizeof error);
    if (!destination || !nav_leaf_name_copy(leaf, name) || !strcmp(name, ".") || !strcmp(name, "..") ||
        strpbrk(name, "/\\:") || strcspn(name, "\r\n\t") != strlen(name) ||
        destination->location(destination, directory, &parent, error, sizeof error) ||
        destination->location_child(destination, &parent, name, &target, error, sizeof error)) {
        const char *lines[] = {error[0] ? error : "Invalid destination filename"}; nav_ui_info(" Download Error ", lines, 1); return -1;
    }
    NavEntry existing; bool overwrite = false;
    if (destination->stat && !destination->stat(destination, target.resource_id, &existing, error, sizeof error)) {
        if (existing.flags & NAV_ENTRY_DIR) { const char *lines[] = {"Destination is a directory"}; nav_ui_info(" Download Error ", lines, 1); return -1; }
        if (config->confirm_overwrite && !nav_ui_confirm("Overwrite destination?", redraw, data)) return 0;
        overwrite = true;
    }
    DownloadScreen screen = {.redraw = redraw, .data = data, .name = name};
    int result = nav_download_copy(source, entry->resource_id, destination, target.resource_id,
                                   overwrite, config->transfer_buffer_size, download_progress,
                                   download_cancel, &screen, error, sizeof error);
    const char *lines[] = {result ? error : "Download complete"};
    nav_ui_info(result ? " Download Error " : " Download ", lines, 1);
    return result ? -1 : 1;
}
