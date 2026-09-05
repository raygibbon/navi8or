/* Read-only movement/search state adapted from TDX movement.c/findrep.c. */
#include "nav_view.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t count(const NavViewer *viewer){return viewer->source->line_count(viewer->source);}

void nav_viewer_init(NavViewer *viewer,NavViewSource *source){
    *viewer=(NavViewer){.source=source,.line_numbers=true,.match_line=SIZE_MAX};
}
void nav_viewer_ensure_visible(NavViewer *viewer,size_t page){
    if(!page)page=1;
    if(viewer->current_line<viewer->top_line)viewer->top_line=viewer->current_line;
    else if(viewer->current_line>=viewer->top_line+page)viewer->top_line=viewer->current_line-page+1;
}
void nav_viewer_move(NavViewer *viewer,long delta,size_t page){
    size_t lines=count(viewer);
    if(!lines)return;
    if(delta<0){size_t amount=(size_t)(-delta);viewer->current_line=viewer->current_line>amount?viewer->current_line-amount:0;}
    else if((size_t)delta>=lines-1-viewer->current_line)viewer->current_line=lines-1;
    else viewer->current_line+=(size_t)delta;
    nav_viewer_ensure_visible(viewer,page);
}
void nav_viewer_page(NavViewer *viewer,long direction,size_t page){nav_viewer_move(viewer,direction<0?-(long)page:(long)page,page);}
void nav_viewer_top(NavViewer *viewer){viewer->current_line=viewer->top_line=0;}
void nav_viewer_bottom(NavViewer *viewer,size_t page){size_t lines=count(viewer);if(lines)viewer->current_line=lines-1;nav_viewer_ensure_visible(viewer,page);}
void nav_viewer_horizontal(NavViewer *viewer,long delta){
    if(delta<0){size_t amount=(size_t)(-delta);viewer->horizontal_offset=viewer->horizontal_offset>amount?viewer->horizontal_offset-amount:0;}
    else viewer->horizontal_offset+=(size_t)delta;
}
size_t nav_viewer_line_number_width(const NavViewer *viewer){
    size_t lines=count(viewer),digits=1;
    while(lines>=10){digits++;lines/=10;}
    return viewer->line_numbers?digits+2:0;
}
void nav_viewer_goto_line(NavViewer *viewer,size_t one_based,size_t page){
    size_t lines=count(viewer);
    if(!lines){viewer->current_line=viewer->top_line=0;return;}
    if(one_based<1)one_based=1;
    if(one_based>lines)one_based=lines;
    viewer->current_line=one_based-1;nav_viewer_ensure_visible(viewer,page);
}
bool nav_viewer_find(NavViewer *viewer,int direction,size_t page,bool *wrapped){
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
