#include "tdxui.h"
#include <assert.h>
#include <string.h>

static TdxUiFieldResult key(TdxUiField *field,int value,unsigned modifiers){
    NavTermEvent event={NAV_TERM_EVENT_KEY,value,modifiers,0,0};
    return tdxui_field_event(field,&event);
}

static void test_field(void){
    char buffer[16]="abcd";
    TdxUiField field;
    tdxui_field_init(&field,buffer,sizeof buffer);
    assert(field.cursor==4&&field.length==4);
    assert(key(&field,NAV_KEY_LEFT,0)==TDXUI_FIELD_MOVED);
    assert(key(&field,NAV_KEY_LEFT,0)==TDXUI_FIELD_MOVED);
    assert(key(&field,'X',0)==TDXUI_FIELD_CHANGED);
    assert(strcmp(buffer,"abXcd")==0&&field.cursor==3);
    assert(key(&field,NAV_KEY_BACKSPACE,0)==TDXUI_FIELD_CHANGED);
    assert(strcmp(buffer,"abcd")==0&&field.cursor==2);
    assert(key(&field,NAV_KEY_DELETE,0)==TDXUI_FIELD_CHANGED);
    assert(strcmp(buffer,"abd")==0);
    assert(key(&field,NAV_KEY_HOME,0)==TDXUI_FIELD_MOVED&&field.cursor==0);
    assert(key(&field,NAV_KEY_END,0)==TDXUI_FIELD_MOVED&&field.cursor==3);
    assert(key(&field,'q',NAV_MOD_CTRL)==TDXUI_FIELD_IGNORED);
    field.cursor=3;field.offset=0;tdxui_field_ensure_visible(&field,2);
    assert(field.offset==2);
}

static void test_menu(void){
    static const TdxUiMenuItem items[]={
        {"One",10,NULL,false,false,'o'},
        {NULL,0,NULL,true,true,0},
        {"Disabled",20,NULL,true,false,'d'},
        {"Three",30,NULL,false,false,'t'}
    };
    TdxUiMenu menu={"Test",items,4,0};
    NavTermEvent event={NAV_TERM_EVENT_KEY,NAV_KEY_RIGHT,NAV_MOD_CTRL,0,0};
    size_t selected=99;
    assert(tdxui_menu_move_minor(&menu,0,1)==2);
    assert(tdxui_menu_move_minor(&menu,2,1)==3);
    assert(tdxui_menu_move_minor(&menu,0,-1)==3);
    assert(tdxui_menu_move_major(2,3,1)==0);
    assert(tdxui_menu_move_major(0,3,-1)==2);
    assert(tdxui_menu_major_motion(&event)==1);
    event.key=NAV_KEY_LEFT;
    assert(tdxui_menu_major_motion(&event)==-1);
    event.modifiers=NAV_MOD_ALT;
    assert(tdxui_menu_major_motion(&event)==0);
    assert(tdxui_menu_accelerator(&menu,'T',&selected)==30&&selected==3);
    assert(tdxui_menu_accelerator(&menu,'d',&selected)==TDXUI_MENU_CANCELLED);
}

static void test_text_view(void){
    TdxUiTextView view={0};
    tdxui_text_view_key(&view,NAV_KEY_PAGE_DOWN,30,10);
    assert(view.top==10);
    tdxui_text_view_key(&view,NAV_KEY_END,30,10);
    assert(view.top==20);
    tdxui_text_view_key(&view,NAV_KEY_DOWN,30,10);
    assert(view.top==20);
    tdxui_text_view_key(&view,NAV_KEY_PAGE_UP,30,10);
    assert(view.top==10);
    tdxui_text_view_key(&view,NAV_KEY_HOME,30,10);
    assert(view.top==0);
}

int main(void){test_field();test_menu();test_text_view();return 0;}
