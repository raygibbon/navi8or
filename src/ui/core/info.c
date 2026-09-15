/* Adapted from TDX src/core/hwind.c:show_help(), show_strings(), and
 * adjust_area().  Navi8or supplies variable text and scrolling; the centered
 * Help-region save/draw/shadow/restore lifecycle follows TDX. */
#include "nav_ui_core.h"
#include <string.h>

static int text_width(const char *const *lines, size_t count)
{
    int width = 0;
    for (size_t index = 0; index < count; index++) {
        int length = lines && lines[index] ? (int)strlen(lines[index]) : 0;
        if (length > width)
            width = length;
    }
    return width;
}

void nav_ui_info(const char *title, const char *const *lines, size_t count)
{
    NavUiTextView view = {0};
    NavUiArea saved = {0};
    NavUiOutputContext output;
    int saved_valid = 0;
    int base_invalid = 0;
    for (;;) {
        NavAction event;
        int screen_width = nav_term_width(), screen_height = nav_term_height();
        int width = text_width(lines, count) + 4;
        int height = (int)count + 4;
        int x, y, page;
        if (width < 30)
            width = 30;
        if (width > screen_width - 2)
            width = screen_width - 2;
        if (height > screen_height - 2)
            height = screen_height - 2;
        page = height - 2;
        if (page < 0)
            page = 0;
        nav_ui_text_view_clamp(&view, count, (size_t)page);
        x = (screen_width - width) / 2;
        /* TDX: (mode_line - len) / 2. */
        y = (screen_height - 1 - height) / 2;
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;

        /* Initially, the caller's already-presented screen is the TDX
         * underlay to save.  A resize invalidates that geometry; without a
         * redraw callback, rebuild a neutral base for the new terminal. */
        if (base_invalid) {
            nav_term_clear(NAV_STYLE_BACKGROUND);
            base_invalid = 0;
        }
        output = nav_ui_output_context(true);
        if (!saved_valid && width >= 3 && height >= 2) {
            if (nav_ui_save_area(&saved, x, y, width, height, &output) == 0)
                saved_valid = 1;
        }
        if (screen_width >= 20 && screen_height >= 8) {
            nav_ui_frame(x, y, width, height, NULL, 0, title, NAV_STYLE_DIALOG);
            for (int row = 0; row < page && view.top + (size_t)row < count; row++)
                nav_ui_text(x + 2, y + 1 + row, width - 4,
                            lines[view.top + (size_t)row], NAV_STYLE_DIALOG);
        } else
            nav_ui_text(0, 0, screen_width, "Terminal too small", NAV_STYLE_MENU);
        nav_term_hide_cursor();
        nav_term_present();
        if (nav_ui_input(NAV_CONTEXT_INFO, &event) <= 0)
            continue;
        if (event.type == NAV_TERM_EVENT_RESIZE) {
            nav_ui_free_area(&saved);
            saved_valid = 0;
            base_invalid = 1;
            continue;
        }
        if (event.type != NAV_TERM_EVENT_KEY)
            continue;
        if (event.command == NAV_CMD_CANCEL) {
            if (saved_valid)
                nav_ui_restore_area(&saved);
            nav_ui_free_area(&saved);
            nav_term_present();
            return;
        }
        nav_ui_text_view_command(&view, event.command, count, (size_t)page);
    }
}
