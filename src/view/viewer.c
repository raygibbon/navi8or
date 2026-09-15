/* Read-only movement/search state adapted from TDX movement.c/findrep.c. */
#include "nav_view.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static bool link_next(const char *text, size_t length, size_t from, NavViewLink *link)
{
    for (size_t start = from; start < length; start++) {
        size_t prefix = length - start >= 8 && !memcmp(text + start, "https://", 8) ? 8 :
                        length - start >= 7 && !memcmp(text + start, "http://", 7) ? 7 : 0;
        if (!prefix) continue;
        size_t end = start + prefix;
        while (end < length && !isspace((unsigned char)text[end]) &&
               (unsigned char)text[end] >= 32 && text[end] != 127 &&
               !strchr("\"'<>`", text[end])) end++;
        /* Quotes/whitespace delimit URLs. Trim prose punctuation and only
         * unmatched closing brackets, retaining balanced URL parentheses. */
        while (end > start + prefix) {
            char last = text[end - 1];
            if (strchr(".,;!", last)) { end--; continue; }
            const char opens[] = "([{", closes[] = ")]}";
            const char *close = strchr(closes, last);
            if (!close) break;
            size_t which = (size_t)(close - closes);
            int balance = 0;
            for (size_t i = start + prefix; i < end; i++) {
                if (text[i] == opens[which]) balance++;
                if (text[i] == closes[which]) balance--;
            }
            if (balance >= 0) break;
            end--;
        }
        if (end == start + prefix) continue;
        *link = (NavViewLink){start, end - start}; return true;
    }
    return false;
}

bool nav_view_link_at(const char *text, size_t length, size_t column, NavViewLink *link)
{
    NavViewLink candidate;
    for (size_t from = 0; link_next(text, length, from, &candidate);
         from = candidate.column + candidate.length) {
        if (column >= candidate.column && column < candidate.column + candidate.length) {
            *link = candidate; return true;
        }
    }
    return false;
}

/* Walk lazily through source lines; never index or buffer the whole resource. */
bool nav_viewer_link(NavViewer *viewer, int direction, char *url, size_t capacity)
{
    size_t logical = viewer->current_line;
    NavViewCursor cursor = viewer->cursor;
    bool remote = viewer->source->cursor_line != NULL, first = true;
    for (;;) {
        size_t length = 0;
        const char *text = remote ? viewer->source->cursor_line(viewer->source, &cursor, &length) :
                                  viewer->source->line(viewer->source, logical, &length);
        if (!text) break;
        NavViewLink link = {0}, candidate;
        bool found = false;
        if (!direction) found = nav_view_link_at(text, length, viewer->link_length ? viewer->link_column : viewer->horizontal_offset, &link);
        else {
            size_t limit = viewer->link_length ? viewer->link_column : viewer->horizontal_offset;
            for (size_t from = 0; link_next(text, length, from, &candidate);
                 from = candidate.column + candidate.length) {
                if (first && (direction > 0 ? candidate.column < limit + (viewer->link_length ? viewer->link_length : 0) :
                                               candidate.column >= limit)) continue;
                link = candidate; found = true;
                if (direction > 0) break;
            }
        }
        if (found) {
            if (link.length >= capacity) {
                snprintf(viewer->status, sizeof viewer->status, "URL exceeds supported length"); return false;
            }
            memcpy(url, text + link.column, link.length); url[link.length] = 0;
            viewer->current_line = logical; viewer->cursor = cursor;
            viewer->link_column = link.column;
            viewer->link_length = link.length;
            if (remote && !first) { viewer->top_cursor = cursor; viewer->cursor_row = 0; }
            return true;
        }
        if (!direction) break;
        if (remote) {
            long moved = 0;
            if (viewer->source->cursor_move(viewer->source, &cursor, direction > 0 ? 1 : -1, &moved) || !moved) break;
        } else if (direction > 0) {
            if (++logical >= viewer->source->line_count(viewer->source)) break;
        } else { if (!logical) break; logical--; }
        first = false;
    }
    snprintf(viewer->status, sizeof viewer->status, direction ? "No more links" : "No HTTP/HTTPS URL at cursor");
    return false;
}

static bool cursor_mode(const NavViewer *viewer){return viewer->source->cursor_line!=NULL;}
static size_t count(const NavViewer *viewer){return cursor_mode(viewer)?0:viewer->source->line_count(viewer->source);}

/* Remote sources discover line ordinals lazily.  Keep their gutter stable for
   the lifetime of the Viewer instead of reflowing at 10/100/etc. */
#define NAV_VIEWER_REMOTE_LINE_NUMBER_DIGITS 4u

void nav_viewer_init(NavViewer *viewer,NavViewSource *source){
    *viewer=(NavViewer){.source=source,.line_numbers=true,
                        .line_number_digits=NAV_VIEWER_REMOTE_LINE_NUMBER_DIGITS,
                        .match_line=SIZE_MAX};
    if(source->cursor_top){source->cursor_top(source,&viewer->cursor);viewer->top_cursor=viewer->cursor;}
}
void nav_viewer_ensure_visible(NavViewer *viewer,size_t page){
    if(cursor_mode(viewer))return;
    if(!page)page=1;
    if(viewer->current_line<viewer->top_line)viewer->top_line=viewer->current_line;
    else if(viewer->current_line>=viewer->top_line+page)viewer->top_line=viewer->current_line-page+1;
}
void nav_viewer_move(NavViewer *viewer,long delta,size_t page){
    if(cursor_mode(viewer)){
        long moved=0;
        if(!page)page=1;
        if(viewer->source->cursor_move(viewer->source,&viewer->cursor,delta,&moved))return;
        if(moved>0){
            size_t amount=(size_t)moved;
            if(viewer->cursor_row+amount<page)viewer->cursor_row+=amount;
            else{
                size_t shift=viewer->cursor_row+amount-page+1;long shifted=0;
                viewer->source->cursor_move(viewer->source,&viewer->top_cursor,(long)shift,&shifted);
                viewer->cursor_row=page-1;
            }
        }else if(moved<0){
            size_t amount=(size_t)(-moved);
            if(amount<=viewer->cursor_row)viewer->cursor_row-=amount;
            else{viewer->top_cursor=viewer->cursor;viewer->cursor_row=0;}
        }
        return;
    }
    size_t lines=count(viewer);
    if(!lines)return;
    if(delta<0){size_t amount=(size_t)(-delta);viewer->current_line=viewer->current_line>amount?viewer->current_line-amount:0;}
    else if((size_t)delta>=lines-1-viewer->current_line)viewer->current_line=lines-1;
    else viewer->current_line+=(size_t)delta;
    nav_viewer_ensure_visible(viewer,page);
}
void nav_viewer_page(NavViewer *viewer,long direction,size_t page){
    if(cursor_mode(viewer)){
        long moved=0;
        viewer->source->cursor_move(viewer->source,&viewer->cursor,
                                    direction<0?-(long)page:(long)page,&moved);
        viewer->top_cursor=viewer->cursor;viewer->cursor_row=0;return;
    }
    nav_viewer_move(viewer,direction<0?-(long)page:(long)page,page);
}
void nav_viewer_top(NavViewer *viewer){
    if(cursor_mode(viewer)){viewer->source->cursor_top(viewer->source,&viewer->cursor);viewer->top_cursor=viewer->cursor;viewer->cursor_row=0;return;}
    viewer->current_line=viewer->top_line=0;
}
void nav_viewer_bottom(NavViewer *viewer,size_t page){
    if(cursor_mode(viewer)){
        long moved=0;
        if(viewer->source->cursor_bottom(viewer->source,&viewer->cursor))return;
        viewer->top_cursor=viewer->cursor;
        viewer->source->cursor_move(viewer->source,&viewer->top_cursor,
                                    page>1?-(long)(page-1):0,&moved);
        viewer->cursor_row=(size_t)(-moved);return;
    }
    size_t lines=count(viewer);if(lines)viewer->current_line=lines-1;nav_viewer_ensure_visible(viewer,page);
}
void nav_viewer_horizontal(NavViewer *viewer,long delta){
    if(delta<0){size_t amount=(size_t)(-delta);viewer->horizontal_offset=viewer->horizontal_offset>amount?viewer->horizontal_offset-amount:0;}
    else viewer->horizontal_offset+=(size_t)delta;
}
size_t nav_viewer_line_number_width(NavViewer *viewer){
    size_t lines,digits=1;
    if(cursor_mode(viewer)){
        uint64_t number;
        if(!viewer->line_numbers)return 0;
        number=viewer->cursor.ordinal == UINT64_MAX ? UINT64_MAX :
               viewer->cursor.ordinal+1;
        if(viewer->cursor.ordinal_known)
            while(number>=10){digits++;number/=10;}
        if(viewer->line_number_digits < NAV_VIEWER_REMOTE_LINE_NUMBER_DIGITS)
            viewer->line_number_digits=NAV_VIEWER_REMOTE_LINE_NUMBER_DIGITS;
        if(digits > viewer->line_number_digits)
            viewer->line_number_digits=digits;
        return viewer->line_number_digits+2;
    }
    lines=count(viewer);
    while(lines>=10){digits++;lines/=10;}
    return viewer->line_numbers?digits+2:0;
}
void nav_viewer_note_visible_ordinal(NavViewer *viewer,
                                     const NavViewCursor *cursor){
    uint64_t number;
    size_t digits=1;
    if(!viewer->line_numbers || !cursor || !cursor->ordinal_known)return;
    number=cursor->ordinal == UINT64_MAX ? UINT64_MAX : cursor->ordinal+1;
    while(number>=10){digits++;number/=10;}
    if(digits>viewer->line_number_digits)viewer->line_number_digits=digits;
}
void nav_viewer_goto_line(NavViewer *viewer,size_t one_based,size_t page){
    if(cursor_mode(viewer)){
        long moved=0;
        nav_viewer_top(viewer);if(one_based<1)one_based=1;
        viewer->source->cursor_move(viewer->source,&viewer->cursor,
                                    one_based-1>(size_t)LONG_MAX?LONG_MAX:(long)(one_based-1),&moved);
        viewer->top_cursor=viewer->cursor;viewer->cursor_row=0;(void)page;return;
    }
    size_t lines=count(viewer);
    if(!lines){viewer->current_line=viewer->top_line=0;return;}
    if(one_based<1)one_based=1;
    if(one_based>lines)one_based=lines;
    viewer->current_line=one_based-1;nav_viewer_ensure_visible(viewer,page);
}
bool nav_viewer_find(NavViewer *viewer,int direction,size_t page,bool *wrapped){
    if(cursor_mode(viewer)){
        NavViewCursor found;size_t column=0;bool did_wrap=false;
        if(wrapped)*wrapped=false;
        if(!viewer->search[0])return false;
        if(viewer->source->cursor_find(viewer->source,&viewer->cursor,
                                      viewer->search,direction,&found,&column,&did_wrap)){
            viewer->cursor=viewer->top_cursor=found;viewer->cursor_row=0;
            viewer->match_column=column;viewer->match_length=strlen(viewer->search);
            if(wrapped)*wrapped=did_wrap;
            snprintf(viewer->status,sizeof viewer->status,"%s",did_wrap?"Search wrapped":"Match found");
            (void)page;return true;
        }
        viewer->match_length=0;snprintf(viewer->status,sizeof viewer->status,"No match");return false;
    }
    size_t lines=count(viewer),start,index;
    if(wrapped)*wrapped=false;
    if(!lines||!viewer->search[0])return false;
    start=viewer->current_line;
    for(size_t checked=0;checked<lines;checked++){
        if(direction>0)index=(start+1+checked)%lines;
        else index=(start+lines-1-(checked%lines))%lines;
        size_t length=0;const char *line=viewer->source->line(viewer->source,index,&length);
        const char *match=line?strstr(line,viewer->search):NULL;
        if(match){
            bool did_wrap=direction>0?index<=start:index>=start;
            viewer->current_line=viewer->match_line=index;
            viewer->match_column=(size_t)(match-line);
            viewer->match_length=strlen(viewer->search);
            viewer->horizontal_offset=viewer->match_column;
            nav_viewer_ensure_visible(viewer,page);
            if(wrapped)*wrapped=did_wrap;
            snprintf(viewer->status,sizeof viewer->status,"%s",did_wrap?"Search wrapped":"Match found");
            return true;
        }
    }
    viewer->match_line=SIZE_MAX;viewer->match_length=0;
    snprintf(viewer->status,sizeof viewer->status,"No match");
    return false;
}
