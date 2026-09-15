#include "nav.h"
#include "fake_provider.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { uint64_t done,total; int calls; } Progress;
static void progress(uint64_t done,uint64_t total,bool known,void *data){Progress*p=data;(void)known;p->done=done;p->total=total;p->calls++;}
static void test_synthetic_brief(void)
{
    char error[128] = {0}, display[64]; NavPane pane = {0}; NavLocation folder, root; NavPanelFullLayout full;
    pane.provider = nav_test_fake_provider(); pane.history.current = -1; pane.view = NAV_PANEL_BRIEF;
    assert(nav_pane_open(&pane, "root:main", true, true, error, sizeof error) == 0);
    assert(pane.listing.count == 77); assert(!strcmp(pane.listing.items[1].resource_id, "folder:alpha"));
    nav_pane_set_layout(&pane, 56, 5); assert(pane.visible_columns == 2); assert(nav_pane_page_capacity(&pane) == 10);
    pane.selected = 2; nav_pane_move(&pane, 5); assert(pane.selected == 7 && pane.offset == 0);
    nav_pane_move(&pane, 5); assert(pane.selected == 12 && pane.offset == 5);
    nav_pane_move(&pane, -5); assert(pane.selected == 7 && pane.offset == 5);
    pane.selected = 2; pane.offset = 0; nav_pane_page(&pane, 1); assert(pane.selected == 12 && pane.offset == 5);
    nav_pane_page(&pane, -1); assert(pane.selected == 2 && pane.offset == 0);
    pane.selected = 37; nav_pane_set_layout(&pane, 84, 5); assert(pane.selected == 37 && pane.visible_columns == 3);
    nav_pane_set_layout(&pane, 28, 5); assert(pane.selected == 37 && pane.offset == 35);
    nav_pane_set_layout(&pane, 56, 5);
    pane.selected = 76; nav_pane_ensure_visible(&pane); assert(pane.offset == 70);
    pane.listing.count = 8; pane.selected = 4; pane.offset = 0; nav_pane_move(&pane, 5); assert(pane.selected == 7);
    pane.listing.count = 77; assert(pane.provider->location(pane.provider, "folder:alpha", &folder, error, sizeof error) == 0);
    assert(nav_pane_load(&pane, &folder, true, true, error, sizeof error) == 0); assert(pane.listing.count == 2);
    assert(pane.provider->location_parent(pane.provider, &pane.location, &root, error, sizeof error) == 0);
    assert(nav_pane_load(&pane, &root, true, true, error, sizeof error) == 0); assert(pane.listing.count == 77);
    pane.selected = 12; assert(nav_pane_refresh(&pane, true, error, sizeof error) == 0); assert(pane.selected == 12);
    nav_entry_format_display_name(&pane.listing.items[1], display, sizeof display); assert(!strcmp(display, "alpha/"));
    nav_panel_full_layout(70, &full); assert(full.show_size && full.show_modified && full.name_width >= 30 && full.name_width <= 36);
    assert(full.size_width == 12 && full.modified_width == 16);
    assert(full.size_column == full.name_width + 2);
    assert(full.modified_column == full.size_column + full.size_width + 2);
    nav_panel_full_layout(35, &full); assert(full.show_size && !full.show_modified);
    nav_panel_full_layout(20, &full); assert(!full.show_size && !full.show_modified);
    nav_panel_full_layout(72, &full);
    assert(full.name_width == 38 && full.size_column == 40);
    assert(full.modified_column == 54 && full.modified_column + full.modified_width == 70);
    nav_panel_full_layout(40, &full);
    assert(full.show_size && !full.show_modified && full.name_width == 24 && full.size_column == 26);
    nav_panel_full_layout(20, &full);
    assert(full.name_width == 18 && !full.show_size && !full.show_modified);
    nav_listing_free(&pane.listing);
}
static void test_commander_layout(void)
{
    const int widths[] = {120, 81, 64, 28, 20};
    NavCommanderLayout layout;
    NavFunctionKeySegment segments[12];
    NavKeymap map; nav_keymap_defaults(&map);
    for (size_t test = 0; test < sizeof widths / sizeof *widths; test++) {
        size_t count;
        assert(nav_commander_layout(widths[test], 25, &layout));
        assert(layout.top_row == 0 && layout.menu_row == 0);
        assert(layout.pane_title_row == 1);
        assert(layout.pane_location_row == 2);
        assert(layout.pane_column_header_row == 3);
        assert(layout.pane_column_separator_row == -1 && layout.body_top == 4);
        assert(layout.summary_row == 22 && layout.status_row == 23 && layout.key_bar_row == 24);
        assert(layout.body_top + layout.body_height == layout.summary_row);
        assert(layout.body_bottom + 1 == layout.summary_row);
        assert(layout.pane_x[1] == layout.divider + 1);
        assert(layout.pane_x[1] + layout.pane_width[1] == widths[test]);
        assert(layout.pane_width[0] + layout.pane_width[1] + 1 == widths[test]);
        count = nav_function_key_layout(widths[test], &map, NAV_CONTEXT_PANEL, segments, 12);
        assert(count == 9 && segments[0].key == 1 && segments[1].key == 2 && segments[8].key == 10);
        for (size_t index = 0; index < count; index++) {
            assert(segments[index].x >= 0 && segments[index].width >= segments[index].key_width);
            assert(segments[index].x + segments[index].width <= widths[test]);
            if (index) assert(segments[index].x == segments[index - 1].x + segments[index - 1].width);
        }
        assert(segments[count - 1].x + segments[count - 1].width == widths[test]);
        assert(!strcmp(segments[6].label, "MkDir"));
        assert(segments[6].command == NAV_CMD_MKDIR);
    }
    assert(nav_commander_layout_for_style(80, 25, NAV_UI_STYLE_CLASSIC,
                                          &layout));
    assert(layout.pane_column_separator_row == 4 && layout.body_top == 5);
    assert(layout.body_height == 17);
    assert(nav_commander_layout_for_style(80, 25, NAV_UI_STYLE_MODERN,
                                          &layout));
    assert(layout.pane_column_separator_row == -1 && layout.body_top == 4);
    assert(layout.body_height == 18);
    assert(!nav_commander_layout(19, 25, &layout));
    assert(!nav_commander_layout(80, 7, &layout));
    assert(nav_commander_layout(80, 8, &layout) && layout.body_height == 1);
    assert(nav_commander_layout(80, 9, &layout) && layout.body_height == 2);
    assert(!nav_commander_layout_for_style(80, 8, NAV_UI_STYLE_CLASSIC,
                                           &layout));
    assert(nav_commander_layout_for_style(80, 9, NAV_UI_STYLE_CLASSIC,
                                          &layout) && layout.body_height == 1);
    assert(nav_function_key_layout(18, &map, NAV_CONTEXT_PANEL, segments, 12) == 0);
    {
        NavEntry unknown = {0}, known = {.flags = NAV_ENTRY_SIZE_KNOWN};
        assert(!nav_entry_has_known_size(NULL));
        assert(!nav_entry_has_known_size(&unknown));
        assert(nav_entry_has_known_size(&known));
    }
    {
        NavEntry entry = {.size = 1536 * 1024,
                          .modified = 1757116800,
                          .flags = NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN};
        NavPanelFullLayout full;
        char row[128], size[32];
        memset(entry.name, 'x', sizeof entry.name - 1);
        entry.name[sizeof entry.name - 1] = 0;
        nav_format_size(1024, size, sizeof size); assert(!strcmp(size, "1.0 KB"));
        nav_format_size(entry.size, size, sizeof size); assert(!strcmp(size, "1.5 MB"));
        nav_format_size(266, size, sizeof size); assert(!strcmp(size, "266 B"));
        nav_format_size(999u * 1024u, size, sizeof size); assert(!strcmp(size, "999 KB"));
        nav_format_size(17u * 1024u * 1024u / 10u, size, sizeof size); assert(!strcmp(size, "1.7 MB"));
        nav_panel_full_layout(70, &full);
        nav_format_entry_full(&entry, 70, row, sizeof row);
        assert((int)strlen(row) == 68);
        assert(!memcmp(row + full.size_column + full.size_width - 6, "1.5 MB", 6));
        assert(row[full.name_width] == ' ');
        assert(row[full.modified_column] >= '0' && row[full.modified_column] <= '9');
        assert(strstr(row, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx") == NULL);
    }
    {
        NavEntry directory = {.flags = NAV_ENTRY_DIR};
        NavEntry unknown = {0};
        NavEntry sizes[] = {
            {.size = 0, .flags = NAV_ENTRY_SIZE_KNOWN},
            {.size = 266, .flags = NAV_ENTRY_SIZE_KNOWN},
            {.size = 32u * 1024u / 10u, .flags = NAV_ENTRY_SIZE_KNOWN},
            {.size = 118u * 1024u, .flags = NAV_ENTRY_SIZE_KNOWN},
            {.size = 999u * 1024u, .flags = NAV_ENTRY_SIZE_KNOWN},
            {.size = 17u * 1024u * 1024u / 10u, .flags = NAV_ENTRY_SIZE_KNOWN},
            {.size = 124ULL * 1024u * 1024u * 1024u / 10u,
             .flags = NAV_ENTRY_SIZE_KNOWN}
        };
        const char *expected_sizes[] = {
            "0 B", "266 B", "3.2 KB", "118 KB", "999 KB", "1.7 MB",
            "12.4 GB"
        };
        NavPanelFullLayout full;
        char header[128], row[128];
        const int widths[] = {72, 40, 20, 72};
        nav_panel_full_layout(72, &full);
        nav_format_panel_header(NAV_PANEL_FULL, NAV_SORT_NAME, 72,
                                header, sizeof header);
        assert(!memcmp(header, "Name ^", 6));
        assert(!memcmp(header + full.size_column + full.size_width - 4,
                       "Size", 4));
        assert(!memcmp(header + full.modified_column, "Modified", 8));
        nav_format_panel_header(NAV_PANEL_FULL, NAV_SORT_SIZE, 72,
                                header, sizeof header);
        assert(!memcmp(header + full.size_column + full.size_width - 6,
                       "Size ^", 6));
        assert(header[full.modified_column] == 'M');
        for (size_t index = 0; index < sizeof sizes / sizeof *sizes; index++) {
            size_t length = strlen(expected_sizes[index]);
            nav_format_entry_full(&sizes[index], 72, row, sizeof row);
            assert(!memcmp(row + full.size_column + full.size_width - (int)length,
                           expected_sizes[index], length));
            assert(row[full.modified_column] == '-');
        }
        nav_format_entry_full(&directory, 72, row, sizeof row);
        assert(!memcmp(row + full.size_column + full.size_width - 5,
                       "<DIR>", 5));
        nav_format_entry_full(&unknown, 72, row, sizeof row);
        assert(row[full.size_column + full.size_width - 1] == '-');
        snprintf(unknown.name, sizeof unknown.name, "%s",
                 "navi8or-source.tar.gz");
        nav_format_entry_full(&unknown, 72, row, sizeof row);
        assert(!memcmp(row, "navi8or-source.tar.gz", 21));
        snprintf(unknown.name, sizeof unknown.name, "%s",
                 "some-extremely-long-filename-that-must-be-clipped.tar.gz");
        nav_format_entry_full(&unknown, 72, row, sizeof row);
        assert(row[full.name_width] == ' ');
        assert(row[full.size_column + full.size_width - 1] == '-');
        assert(row[full.modified_column] == '-');
        for (size_t index = 0; index < sizeof widths / sizeof *widths; index++) {
            nav_panel_full_layout(widths[index], &full);
            nav_format_panel_header(NAV_PANEL_FULL, NAV_SORT_SIZE,
                                    widths[index], header, sizeof header);
            if (widths[index] == 20) {
                assert(!memcmp(header, "Name", 4));
                assert(strstr(header, "Size") == NULL);
                assert(strstr(header, "Modified") == NULL);
            } else {
                assert(!memcmp(header + full.size_column + full.size_width - 6,
                               "Size ^", 6));
                assert((strstr(header, "Modified") != NULL) ==
                       full.show_modified);
            }
        }
        nav_format_panel_header(NAV_PANEL_BRIEF, NAV_SORT_NAME, 72,
                                header, sizeof header);
        assert(!memcmp(header, "Name ^", 6));
        assert(strstr(header, "Size") == NULL);
        assert(strstr(header, "Modified") == NULL);
    }
}
static void test_leaf_name_bounds(void)
{
    char maximum[NAV_NAME_MAX], oversized[NAV_NAME_MAX + 1], copied[NAV_NAME_MAX];
    memset(maximum, 'a', sizeof maximum - 1);
    maximum[sizeof maximum - 1] = 0;
    memset(oversized, 'b', sizeof oversized - 1);
    oversized[sizeof oversized - 1] = 0;
    assert(nav_leaf_name_copy(copied, maximum));
    assert(!strcmp(copied, maximum));
    assert(!nav_leaf_name_copy(copied, oversized));
}
static void test_path_normalize_component_limit(void)
{
    char long_path[NAV_PATH_MAX] = "/";
    for (int i = 0; i < 513; ++i) {
        if (i > 0) strcat(long_path, "/");
        strcat(long_path, "a");
    }
    char out[NAV_PATH_MAX];
    assert(nav_path_normalize(long_path, out, sizeof out) == -1);
}

static void test_profile_columns(void)
{
    NavConfig config = {.pane_show_size = true, .pane_show_modified = true, .size_bytes = true};
    snprintf(config.date_format, sizeof config.date_format, "%%Y");
    NavEntry entry = {.size = 1536, .modified = 1704067200,
                      .flags = NAV_ENTRY_SIZE_KNOWN | NAV_ENTRY_MODIFIED_KNOWN};
    snprintf(entry.name, sizeof entry.name, "example.txt");
    char row[128], header[128];
    nav_format_entry_full_for_config(&entry, 72, row, sizeof row, &config);
    assert(strstr(row, "1536") && !strstr(row, "KiB"));
    nav_format_panel_header_for_config(NAV_PANEL_FULL, NAV_SORT_NAME, 72, header, sizeof header, &config);
    assert(strstr(header, "Size") && strstr(header, "Modified"));
    config.pane_show_size = config.pane_show_modified = false;
    nav_format_panel_header_for_config(NAV_PANEL_FULL, NAV_SORT_NAME, 72, header, sizeof header, &config);
    assert(!strstr(header, "Size") && !strstr(header, "Modified"));
    nav_format_entry_full_for_config(&entry, 72, row, sizeof row, &config);
    assert(strstr(row, "example.txt") && !strstr(row, "1536"));
}

int main(void){char root[]="/tmp/nav-test-XXXXXX",source[NAV_PATH_MAX],destination[NAV_PATH_MAX],small[NAV_PATH_MAX],error[256];unsigned char data[200000];NavProvider*provider=nav_local_provider();Progress state={0};FILE*file;struct stat st;NavPane pane={0};NavHistory history={.current=-1};NavLocation one={0},two={0},three={0};
    test_profile_columns();
    test_synthetic_brief();
    test_commander_layout();
    test_leaf_name_bounds();
    test_path_normalize_component_limit();
    assert(mkdtemp(root)!=NULL);assert(nav_path_join(root,"file 'with spaces'.bin",source,sizeof source)==0);assert(nav_path_join(root,"copy.bin",destination,sizeof destination)==0);
    for(size_t i=0;i<sizeof data;i++)data[i]=(unsigned char)(i&255);
    file=fopen(source,"wb");assert(file);assert(fwrite(data,1,sizeof data,file)==sizeof data);assert(fclose(file)==0);
    assert(nav_transfer_copy(provider,source,provider,destination,false,progress,&state,error,sizeof error)==0);assert(state.calls>1);assert(state.done==sizeof data&&state.total==sizeof data);assert(stat(destination,&st)==0&&(size_t)st.st_size==sizeof data);
    assert(nav_transfer_copy(provider,source,provider,destination,false,NULL,NULL,error,sizeof error)!=0);assert(nav_transfer_copy(provider,source,provider,source,true,NULL,NULL,error,sizeof error)!=0);
    assert(nav_path_join(root,"small.txt",small,sizeof small)==0);file=fopen(small,"wb");assert(file);assert(fwrite("x",1,1,file)==1);assert(fclose(file)==0);
    pane.provider=provider;pane.history.current=-1;assert(nav_pane_open(&pane,root,true,true,error,sizeof error)==0);nav_pane_sort(&pane,NAV_SORT_SIZE);assert(pane.listing.count==4);assert(pane.listing.items[0].flags&NAV_ENTRY_PARENT);assert(!strcmp(pane.listing.items[1].name,"small.txt"));nav_pane_sort(&pane,NAV_SORT_NAME);assert(!strcmp(pane.listing.items[1].name,"copy.bin"));nav_listing_free(&pane.listing);
    one.provider=two.provider=three.provider=provider;snprintf(one.resource_id,sizeof one.resource_id,"/one");snprintf(two.resource_id,sizeof two.resource_id,"/two");snprintf(three.resource_id,sizeof three.resource_id,"/three");nav_history_push(&history,&one);nav_history_push(&history,&two);assert(!strcmp(nav_history_back(&history)->resource_id,"/one"));nav_history_push(&history,&three);assert(nav_history_forward(&history)==NULL);assert(nav_provider_supports(provider,NAV_CAP_READ|NAV_CAP_WRITE));assert(!nav_provider_supports(provider,256u));
    assert(unlink(small)==0);assert(unlink(destination)==0);assert(unlink(source)==0);assert(rmdir(root)==0);return 0;
}
