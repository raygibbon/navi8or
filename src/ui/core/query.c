/* Adapted from TDX src/core/query.c:get_name(), display_prompt(), get_string(),
 * and get_response().  Editor macros/history are intentionally Navi8or-owned
 * omissions; the saved one-line prompt/query presentation is retained. */
#include "nav_ui_core.h"
#include <stdio.h>
#include <string.h>

static int prompt_row(void)
{
    int height = nav_term_height();
    return height > 0 ? height - 1 : 0;
}

static void draw_prompt(const char *label, NavUiField *field, bool secret)
{
    NavUiOutputContext plain = {false, false, 0};
    int width = nav_term_width(), row = prompt_row();
    int column = label ? (int)strlen(label) : 0;
    int field_width;
    if (width <= 0)
        return;
    if (column > width - 1)
        column = width - 1;
    field_width = width - column;
    nav_ui_s_output(label ? label : "", row, 0, NAV_STYLE_MESSAGE, &plain);
    if (field_width < 1)
        field_width = 1;
    nav_ui_field_draw(field, column, row, field_width, NAV_STYLE_MESSAGE, secret, true);
}

static int prompt_value(const char *title, const char *label, char *buffer,
                        size_t capacity, NavUiRedrawFn redraw, void *data,
                        bool secret)
{
    NavUiArea saved = {0};
    NavUiOutputContext plain = {false, false, 0};
    NavUiField field;
    int saved_valid = 0;
    if (!buffer || !capacity)
        return -1;
    (void)title;
    nav_ui_field_init(&field, buffer, capacity);
    for (;;) {
        NavAction event;
        if (!field.paste && !field.paste_failed) {
            if (redraw)
                redraw(data);
            else
                nav_term_clear(NAV_STYLE_BACKGROUND);
            if (!saved_valid && nav_term_width() > 0) {
                if (nav_ui_save_area(&saved, 0, prompt_row(), nav_term_width(), 1,
                                    &plain) == 0)
                    saved_valid = 1;
            }
            draw_prompt(label, &field, secret);
            nav_term_present();
        }
        if (nav_ui_input(NAV_CONTEXT_DIALOG, &event) <= 0)
            continue;
        if (event.type == NAV_TERM_EVENT_RESIZE) {
            nav_ui_free_area(&saved);
            saved_valid = 0;
            continue;
        }
        switch (nav_ui_field_event(&field, &event)) {
        case NAV_UI_FIELD_ACCEPTED:
            if (saved_valid)
                nav_ui_restore_area(&saved);
            nav_ui_free_area(&saved);
            nav_ui_field_destroy(&field);
            nav_term_hide_cursor();
            nav_term_present();
            return 0;
        case NAV_UI_FIELD_CANCELLED:
            if (saved_valid)
                nav_ui_restore_area(&saved);
            nav_ui_free_area(&saved);
            nav_ui_field_destroy(&field);
            nav_term_hide_cursor();
            nav_term_present();
            return -1;
        case NAV_UI_FIELD_ERROR: {
            const char *lines[] = {field.error}; nav_ui_info(" Text Entry ", lines, 1); break;
        }
        default: break;
        }
    }
}

int nav_ui_prompt_text(const char *title, const char *label, char *buffer,
                       size_t capacity, NavUiRedrawFn redraw, void *data)
{ return prompt_value(title, label, buffer, capacity, redraw, data, false); }

int nav_ui_prompt_secret(const char *title, const char *label, char *buffer,
                         size_t capacity, NavUiRedrawFn redraw, void *data)
{ return prompt_value(title, label, buffer, capacity, redraw, data, true); }

bool nav_ui_confirm(const char *question, NavUiRedrawFn redraw, void *data)
{
    NavUiArea saved = {0};
    NavUiOutputContext plain = {false, false, 0};
    int saved_valid = 0;
    bool accepted = false;
    for (;;) {
        NavAction event;
        char prompt[512];
        if (redraw)
            redraw(data);
        else
            nav_term_clear(NAV_STYLE_BACKGROUND);
        if (!saved_valid && nav_term_width() > 0) {
            if (nav_ui_save_area(&saved, 0, prompt_row(), nav_term_width(), 1,
                                &plain) == 0)
                saved_valid = 1;
        }
        char yes[80], no[80];
        nav_ui_hint_key(NAV_CONTEXT_CONFIRM, NAV_CMD_ACCEPT, false, yes, sizeof yes);
        nav_ui_hint_key(NAV_CONTEXT_CONFIRM, NAV_CMD_CANCEL, false, no, sizeof no);
        if (nav_ui_show_dialog_keys()) snprintf(prompt, sizeof prompt, "%.*s (%s/%s): ", 320, question ? question : "", yes, no);
        else snprintf(prompt, sizeof prompt, "%.*s: ", 490, question ? question : "");
        nav_ui_s_output(prompt, prompt_row(), 0, NAV_STYLE_MESSAGE, &plain);
        {
            int column = (int)strlen(prompt);
            if (column > nav_term_width() - 1)
                column = nav_term_width() - 1;
            nav_ui_text(column, prompt_row(), nav_term_width() - column, "",
                        NAV_STYLE_TEXT);
        }
        nav_term_hide_cursor();
        nav_term_present();
        if (nav_ui_input(NAV_CONTEXT_CONFIRM, &event) <= 0)
            continue;
        if (event.type == NAV_TERM_EVENT_RESIZE) {
            nav_ui_free_area(&saved);
            saved_valid = 0;
            continue;
        }
        if (event.type != NAV_TERM_EVENT_KEY)
            continue;
        if (event.command == NAV_CMD_ACCEPT) {
            accepted = true;
            break;
        }
        if (event.command == NAV_CMD_CANCEL)
            break;
    }
    if (saved_valid)
        nav_ui_restore_area(&saved);
    nav_ui_free_area(&saved);
    nav_term_present();
    return accepted;
}
