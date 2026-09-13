/*
 * Adapted from TDX src/core/query.c edit-field handling.
 * History, editor copy commands, and dialog-global state are intentionally
 * omitted; the cursor, insertion, deletion, and viewport rules are retained.
 */
#include "nav_ui_core.h"
#include <string.h>

void nav_ui_field_init(NavUiField *field,char *buffer,size_t capacity){
    size_t length=capacity?strnlen(buffer,capacity):0;
    if(capacity&&length==capacity){length=capacity-1;buffer[length]=0;}
    *field=(NavUiField){buffer,capacity,length,length,0};
}

void nav_ui_field_ensure_visible(NavUiField *field,size_t width){
    if(width==0){field->offset=field->cursor;return;}
    if(field->cursor<field->offset)field->offset=field->cursor;
    else if(field->cursor>=field->offset+width)field->offset=field->cursor-width+1;
    if(field->offset>field->length)field->offset=field->length;
}

NavUiFieldResult nav_ui_field_event(NavUiField *field,const NavAction *event){
    if(!field||!event||event->type!=NAV_TERM_EVENT_KEY)return NAV_UI_FIELD_IGNORED;
    switch(event->command){
        case NAV_CMD_CANCEL:return NAV_UI_FIELD_CANCELLED;
        case NAV_CMD_ACCEPT:return NAV_UI_FIELD_ACCEPTED;
        case NAV_CMD_HOME:field->cursor=0;return NAV_UI_FIELD_MOVED;
        case NAV_CMD_END:field->cursor=field->length;return NAV_UI_FIELD_MOVED;
        case NAV_CMD_LEFT:
            if(field->cursor){field->cursor--;return NAV_UI_FIELD_MOVED;}
            return NAV_UI_FIELD_IGNORED;
        case NAV_CMD_RIGHT:
            if(field->cursor<field->length){field->cursor++;return NAV_UI_FIELD_MOVED;}
            return NAV_UI_FIELD_IGNORED;
        case NAV_CMD_BACKSPACE:
            if(!field->cursor)return NAV_UI_FIELD_IGNORED;
            memmove(field->buffer+field->cursor-1,field->buffer+field->cursor,
                    field->length-field->cursor+1);
            field->cursor--;field->length--;
            return NAV_UI_FIELD_CHANGED;
        case NAV_CMD_TEXT_DELETE:
            if(field->cursor>=field->length)return NAV_UI_FIELD_IGNORED;
            memmove(field->buffer+field->cursor,field->buffer+field->cursor+1,
                    field->length-field->cursor);
            field->length--;
            return NAV_UI_FIELD_CHANGED;
        default:break;
    }
    if(event->command != NAV_CMD_TEXT||(event->text<32||event->text>=127)||
       field->length+1>=field->capacity)return NAV_UI_FIELD_IGNORED;
    memmove(field->buffer+field->cursor+1,field->buffer+field->cursor,
            field->length-field->cursor+1);
    field->buffer[field->cursor++]=(char)event->text;
    field->length++;
    return NAV_UI_FIELD_CHANGED;
}
