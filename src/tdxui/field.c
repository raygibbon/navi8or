/*
 * Adapted from TDX src/core/query.c edit-field handling.
 * History, editor copy commands, and dialog-global state are intentionally
 * omitted; the cursor, insertion, deletion, and viewport rules are retained.
 */
#include "tdxui.h"
#include <string.h>

void tdxui_field_init(TdxUiField *field,char *buffer,size_t capacity){
    size_t length=capacity?strnlen(buffer,capacity):0;
    if(capacity&&length==capacity){length=capacity-1;buffer[length]=0;}
    *field=(TdxUiField){buffer,capacity,length,length,0};
}

void tdxui_field_ensure_visible(TdxUiField *field,size_t width){
    if(width==0){field->offset=field->cursor;return;}
    if(field->cursor<field->offset)field->offset=field->cursor;
    else if(field->cursor>=field->offset+width)field->offset=field->cursor-width+1;
    if(field->offset>field->length)field->offset=field->length;
}

TdxUiFieldResult tdxui_field_event(TdxUiField *field,const NavTermEvent *event){
    if(!field||!event||event->type!=NAV_TERM_EVENT_KEY)return TDXUI_FIELD_IGNORED;
    switch(event->key){
        case NAV_KEY_ESCAPE:return TDXUI_FIELD_CANCELLED;
        case NAV_KEY_ENTER:return TDXUI_FIELD_ACCEPTED;
        case NAV_KEY_HOME:field->cursor=0;return TDXUI_FIELD_MOVED;
        case NAV_KEY_END:field->cursor=field->length;return TDXUI_FIELD_MOVED;
        case NAV_KEY_LEFT:
            if(field->cursor){field->cursor--;return TDXUI_FIELD_MOVED;}
            return TDXUI_FIELD_IGNORED;
        case NAV_KEY_RIGHT:
            if(field->cursor<field->length){field->cursor++;return TDXUI_FIELD_MOVED;}
            return TDXUI_FIELD_IGNORED;
        case NAV_KEY_BACKSPACE:
            if(!field->cursor)return TDXUI_FIELD_IGNORED;
            memmove(field->buffer+field->cursor-1,field->buffer+field->cursor,
                    field->length-field->cursor+1);
            field->cursor--;field->length--;
            return TDXUI_FIELD_CHANGED;
        case NAV_KEY_DELETE:
            if(field->cursor>=field->length)return TDXUI_FIELD_IGNORED;
            memmove(field->buffer+field->cursor,field->buffer+field->cursor+1,
                    field->length-field->cursor);
            field->length--;
            return TDXUI_FIELD_CHANGED;
        default:break;
    }
    if(event->modifiers||(event->key<32||event->key>=127)||
       field->length+1>=field->capacity)return TDXUI_FIELD_IGNORED;
    memmove(field->buffer+field->cursor+1,field->buffer+field->cursor,
            field->length-field->cursor+1);
    field->buffer[field->cursor++]=(char)event->key;
    field->length++;
    return TDXUI_FIELD_CHANGED;
}
