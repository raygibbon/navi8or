#include "nav_view.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static size_t fake_count(NavViewSource *source){(void)source;return 120023;}
static const char *fake_cursor_line(NavViewSource *source,
                                    const NavViewCursor *cursor, size_t *length)
{ (void)source; (void)cursor; *length = 0; return ""; }

static void test_indexed_source_and_state(void){
    char path[]="/tmp/nav-viewer-XXXXXX",error[128];bool binary=false,wrapped=false;
    int descriptor=mkstemp(path);assert(descriptor>=0);
    FILE *file=fdopen(descriptor,"wb");assert(file);
    fputs("first\n",file);
    char *long_line=malloc(20001);assert(long_line);memset(long_line,'a',20000);memcpy(long_line+17000,"needle",6);long_line[20000]=0;
    fwrite(long_line,1,20000,file);fputc('\n',file);fputs("last needle",file);fclose(file);free(long_line);
    NavViewSource *source=nav_view_source_open_local(path,&binary,error,sizeof error);
    assert(source&&!binary&&source->line_count(source)==3);
    assert(source->line_length(source,1)==20000);
    size_t length=0;const char *line=source->line(source,1,&length);
    assert(line&&length==20000&&!memcmp(line+17000,"needle",6));
    NavViewer viewer;nav_viewer_init(&viewer,source);
    nav_viewer_move(&viewer,1,2);assert(viewer.current_line==1&&viewer.top_line==0);
    nav_viewer_move(&viewer,1,2);assert(viewer.current_line==2&&viewer.top_line==1);
    nav_viewer_page(&viewer,-1,2);assert(viewer.current_line==0&&viewer.top_line==0);
    nav_viewer_horizontal(&viewer,20);nav_viewer_horizontal(&viewer,-5);assert(viewer.horizontal_offset==15);
    nav_viewer_horizontal(&viewer,-30);assert(viewer.horizontal_offset==0);
    nav_viewer_goto_line(&viewer,999,2);assert(viewer.current_line==2);
    nav_viewer_goto_line(&viewer,0,2);assert(viewer.current_line==0);
    snprintf(viewer.search,sizeof viewer.search,"needle");
    assert(nav_viewer_find(&viewer,1,2,&wrapped)&&viewer.current_line==1&&!wrapped&&viewer.match_column==17000);
    assert(nav_viewer_find(&viewer,1,2,&wrapped)&&viewer.current_line==2&&!wrapped);
    assert(nav_viewer_find(&viewer,1,2,&wrapped)&&viewer.current_line==1&&wrapped);
    assert(nav_viewer_find(&viewer,-1,2,&wrapped)&&viewer.current_line==2&&wrapped);
    snprintf(viewer.search,sizeof viewer.search,"absent");
    assert(!nav_viewer_find(&viewer,1,2,&wrapped));
    source->close(source);unlink(path);
}

static void test_empty_binary_and_width(void){
    char empty[]="/tmp/nav-empty-XXXXXX",binary_path[]="/tmp/nav-binary-XXXXXX",error[128];bool binary=false;
    int descriptor=mkstemp(empty);assert(descriptor>=0);close(descriptor);
    NavViewSource *source=nav_view_source_open_local(empty,&binary,error,sizeof error);assert(source&&!binary&&source->line_count(source)==0);source->close(source);unlink(empty);
    descriptor=mkstemp(binary_path);assert(descriptor>=0);assert(write(descriptor,"a\0b",3)==3);close(descriptor);
    source=nav_view_source_open_local(binary_path,&binary,error,sizeof error);assert(!source&&binary);unlink(binary_path);
    NavViewSource fake={.line_count=fake_count};NavViewer viewer;nav_viewer_init(&viewer,&fake);
    assert(nav_viewer_line_number_width(&viewer)==8);viewer.line_numbers=false;assert(nav_viewer_line_number_width(&viewer)==0);

    /* Lazy/range-backed sources must not move content at digit boundaries. */
    NavViewSource remote={.cursor_line=fake_cursor_line};
    nav_viewer_init(&viewer,&remote);
    viewer.cursor.ordinal_known=true;
    const uint64_t boundaries[]={8,9,10,11,98,99,100,101,998,999,1000,1001,
                                 9998,9999};
    for(size_t index=0;index<sizeof boundaries/sizeof *boundaries;index++) {
        viewer.cursor.ordinal=boundaries[index]-1;
        assert(nav_viewer_line_number_width(&viewer)==6);
    }
    viewer.cursor.ordinal=9999;
    assert(nav_viewer_line_number_width(&viewer)==7);
    viewer.cursor.ordinal=9;
    assert(nav_viewer_line_number_width(&viewer)==7);
    viewer.cursor.ordinal=99999;
    assert(nav_viewer_line_number_width(&viewer)==8);
    NavViewCursor visible={.ordinal=9999,.ordinal_known=true};
    nav_viewer_init(&viewer,&remote);
    nav_viewer_note_visible_ordinal(&viewer,&visible);
    assert(nav_viewer_line_number_width(&viewer)==7);
    visible.ordinal=99999;
    nav_viewer_note_visible_ordinal(&viewer,&visible);
    assert(nav_viewer_line_number_width(&viewer)==8);
    visible.ordinal=9;
    nav_viewer_note_visible_ordinal(&viewer,&visible);
    assert(nav_viewer_line_number_width(&viewer)==8);
    viewer.line_numbers=false;assert(nav_viewer_line_number_width(&viewer)==0);
}

static void test_links(void)
{
    const char *text = "Log: (https://example.com/a(b)). Next http://example.com/data?q=x&n=2, end";
    NavViewLink link;
    assert(nav_view_link_at(text, strlen(text), 12, &link));
    assert(link.column == 6 && link.length == strlen("https://example.com/a(b)"));
    assert(!nav_view_link_at(text, strlen(text), 0, &link));
    assert(nav_view_link_at(text, strlen(text), 45, &link));
    assert(link.length == strlen("http://example.com/data?q=x&n=2"));
    assert(!nav_view_link_at("https://", 8, 0, &link));
    char path[] = "/tmp/nav-view-links-XXXXXX", error[128], url[4096];
    int fd = mkstemp(path); assert(fd >= 0);
    FILE *file = fdopen(fd, "w"); assert(file);
    fprintf(file, "%s\nno links\nhttps://example.com/last\n", text); fclose(file);
    bool binary;
    NavViewSource *source = nav_view_source_open_local(path, &binary, error, sizeof error); assert(source);
    NavViewer viewer; nav_viewer_init(&viewer, source);
    assert(nav_viewer_link(&viewer, 1, url, sizeof url));
    assert(!strcmp(url, "https://example.com/a(b)"));
    assert(nav_viewer_link(&viewer, 0, url, sizeof url));
    assert(nav_viewer_link(&viewer, 1, url, sizeof url));
    assert(!strcmp(url, "http://example.com/data?q=x&n=2"));
    assert(nav_viewer_link(&viewer, 1, url, sizeof url)); assert(viewer.current_line == 2);
    assert(nav_viewer_link(&viewer, -1, url, sizeof url)); assert(viewer.current_line == 0);
    assert(!strcmp(url, "http://example.com/data?q=x&n=2"));
    assert(nav_viewer_link(&viewer, -1, url, sizeof url));
    assert(!nav_viewer_link(&viewer, -1, url, sizeof url));
    source->close(source); unlink(path);
}

int main(void){test_indexed_source_and_state();test_empty_binary_and_width();test_links();return 0;}
