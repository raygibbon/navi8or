/* Pure viewport movement shared by TDX-style Help and information windows. */
#include "nav_ui_core.h"

void nav_ui_text_view_clamp(NavUiTextView *view,size_t count,size_t page){
    size_t maximum=count>page?count-page:0;
    if(view->top>maximum)view->top=maximum;
}

void nav_ui_text_view_key(NavUiTextView *view,int key,size_t count,size_t page){
    if(key==NAV_KEY_HOME)view->top=0;
    else if(key==NAV_KEY_END)view->top=count>page?count-page:0;
    else if(key==NAV_KEY_UP&&view->top)view->top--;
    else if(key==NAV_KEY_DOWN&&view->top+(page<count?page:count)<count)view->top++;
    else if(key==NAV_KEY_PAGE_UP)view->top=view->top>page?view->top-page:0;
    else if(key==NAV_KEY_PAGE_DOWN)view->top+=page?page:1;
    nav_ui_text_view_clamp(view,count,page);
}
