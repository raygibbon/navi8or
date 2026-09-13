#include "nav_ui_core.h"
#include <assert.h>
#include <string.h>

static NavUiFieldResult key(NavUiField *field, int value, unsigned modifiers)
{
    NavTermEvent event = {NAV_TERM_EVENT_KEY, value, modifiers, 0, 0};
    NavKeymap map; NavInput input = {0};
    nav_keymap_defaults(&map);
    NavAction action = nav_input_resolve(&input, &map, NAV_CONTEXT_DIALOG, &event);
    return nav_ui_field_event(field, &action);
}

static void test_field(void)
{
    char buffer[16] = "abcd";
    NavUiField field;
    nav_ui_field_init(&field, buffer, sizeof buffer);
    assert(field.cursor == 4 && field.length == 4);
    assert(key(&field, NAV_KEY_LEFT, 0) == NAV_UI_FIELD_MOVED);
    assert(key(&field, NAV_KEY_LEFT, 0) == NAV_UI_FIELD_MOVED);
    assert(key(&field, 'X', 0) == NAV_UI_FIELD_CHANGED);
    assert(strcmp(buffer, "abXcd") == 0 && field.cursor == 3);
    assert(key(&field, NAV_KEY_BACKSPACE, 0) == NAV_UI_FIELD_CHANGED);
    assert(strcmp(buffer, "abcd") == 0 && field.cursor == 2);
    assert(key(&field, NAV_KEY_DELETE, 0) == NAV_UI_FIELD_CHANGED);
    assert(strcmp(buffer, "abd") == 0);
    assert(key(&field, NAV_KEY_HOME, 0) == NAV_UI_FIELD_MOVED && field.cursor == 0);
    assert(key(&field, NAV_KEY_END, 0) == NAV_UI_FIELD_MOVED && field.cursor == 3);
    assert(key(&field, 'q', NAV_MOD_CTRL) == NAV_UI_FIELD_CANCELLED);
    field.cursor = 3;
    field.offset = 0;
    nav_ui_field_ensure_visible(&field, 2);
    assert(field.offset == 2);
}

static int menu_motion(const NavTermEvent *event)
{
    NavKeymap map; NavInput input = {0};
    nav_keymap_defaults(&map);
    NavAction action = nav_input_resolve(&input, &map, NAV_CONTEXT_MENU, event);
    return nav_ui_menu_major_motion(&action);
}

static void test_menu(void)
{
    static const NavUiMenuItem items[] = {
        {"One", 10, NULL, false, false, 'o'},
        {NULL, 0, NULL, true, true, 0},
        {"Disabled", 20, NULL, true, false, 'd'},
        {"Three", 30, NULL, false, false, 't'}};
    NavUiMenu menu = {"Test", items, 4, 0};
    NavTermEvent event = {NAV_TERM_EVENT_KEY, NAV_KEY_RIGHT, NAV_MOD_CTRL, 0, 0};
    size_t selected = 99;
    assert(nav_ui_menu_move_minor(&menu, 0, 1) == 2);
    assert(nav_ui_menu_move_minor(&menu, 2, 1) == 3);
    assert(nav_ui_menu_move_minor(&menu, 0, -1) == 3);
    assert(nav_ui_menu_move_major(2, 3, 1) == 0);
    assert(nav_ui_menu_move_major(0, 3, -1) == 2);
    assert(menu_motion(&event) == 1);
    event.key = NAV_KEY_LEFT;
    assert(menu_motion(&event) == -1);
    event.modifiers = 0;
    assert(menu_motion(&event) == -1);
    event.key = NAV_KEY_RIGHT;
    assert(menu_motion(&event) == 1);
    event.modifiers = NAV_MOD_ALT;
    assert(menu_motion(&event) == 0);
    assert(nav_ui_menu_accelerator(&menu, 'T', &selected) == 30 && selected == 3);
    assert(nav_ui_menu_accelerator(&menu, 'd', &selected) == NAV_UI_MENU_CANCELLED);
    assert(nav_ui_menu_activate(&menu, 0) == 10);
    assert(nav_ui_menu_activate(&menu, 1) == NAV_UI_MENU_CANCELLED);
    assert(nav_ui_menu_activate(&menu, 2) == NAV_UI_MENU_CANCELLED);
    assert(nav_ui_menu_activate(&menu, 3) == 30);
    assert(nav_ui_menu_activate(&menu, 4) == NAV_UI_MENU_CANCELLED);
}

static void test_bar_spacing(void)
{
    static const NavUiMenu menus[] = {{"File", NULL, 0, 0}, {"View", NULL, 0, 0}, {"Search", NULL, 0, 0}, {"Help", NULL, 0, 0}};
    const int widths[] = {40, 80, 100, 120, 160};
    const int expected_columns[][4] = {{1, 11, 21, 33}, {6, 16, 26, 38}, {6, 16, 26, 38}, {6, 16, 26, 38}, {6, 16, 26, 38}};
    for (size_t width_index = 0; width_index < sizeof widths / sizeof *widths; width_index++)
    {
        NavUiMajor major[4];
        nav_ui_get_bar_spacing_for_style(menus, 4, widths[width_index],
                                         NAV_UI_STYLE_CLASSIC, major);
        assert(major[0].width == 4 && major[2].width == 6);
        assert(major[0].column == expected_columns[width_index][0]);
        for (size_t index = 1; index < 4; index++)
        {
            assert(major[index].column == expected_columns[width_index][index]);
            assert(major[index].column >= major[index - 1].column + major[index - 1].width + 1);
        }
    }
    NavUiMajor narrow[4];
    nav_ui_get_bar_spacing_for_style(menus, 4, 40, NAV_UI_STYLE_CLASSIC,
                                     narrow);
    assert(narrow[0].column == 1);
    assert(narrow[1].column == 11);
    nav_ui_get_bar_spacing(menus, 4, 80, narrow);
    assert(narrow[0].column == 1);
    assert(narrow[1].column == 8);
    assert(narrow[2].column == 15);
    assert(narrow[3].column == 24);
}

static void test_text_view(void)
{
    NavUiTextView view = {0};
    nav_ui_text_view_command(&view, NAV_CMD_PAGE_DOWN, 30, 10);
    assert(view.top == 10);
    nav_ui_text_view_command(&view, NAV_CMD_END, 30, 10);
    assert(view.top == 20);
    nav_ui_text_view_command(&view, NAV_CMD_DOWN, 30, 10);
    assert(view.top == 20);
    nav_ui_text_view_command(&view, NAV_CMD_PAGE_UP, 30, 10);
    assert(view.top == 10);
    nav_ui_text_view_command(&view, NAV_CMD_HOME, 30, 10);
    assert(view.top == 0);
}

static void expect_area(bool space, bool shadow, int col, int row, int width,
                        int height, int expected_col, int expected_row,
                        int expected_width, int expected_height,
                        int expected_shadow)
{
    NavUiOutputContext context = {space, shadow, 1};
    NavUiAreaGeometry area;
    nav_ui_adjust_area(&area, width, height, row, col, 80, 25, &context);
    assert(area.col == expected_col && area.row == expected_row);
    assert(area.width == expected_width && area.height == expected_height);
    assert(area.shadow_width == expected_shadow);
}

static void test_adjust_area(void)
{
    expect_area(false, false, 5, 2, 30, 10, 5, 2, 30, 10, 0);
    expect_area(true, false, 5, 2, 30, 10, 4, 2, 32, 10, 0);
    expect_area(false, true, 5, 2, 30, 10, 5, 2, 31, 11, 1);
    expect_area(true, true, 5, 2, 30, 10, 4, 2, 33, 11, 1);

    /* FrameSpace cannot extend past the left or right screen edge. */
    expect_area(true, false, 0, 2, 30, 10, 0, 2, 31, 10, 0);
    expect_area(true, true, 50, 2, 30, 10, 49, 2, 31, 11, 0);

    /* At the bottom edge there is no extra row available for the shadow. */
    expect_area(false, true, 5, 15, 30, 10, 5, 15, 31, 10, 1);
    expect_area(true, true, 5, 15, 30, 10, 4, 15, 33, 10, 1);
}

int main(void)
{
    test_field();
    test_menu();
    test_bar_spacing();
    test_text_view();
    test_adjust_area();
    return 0;
}
