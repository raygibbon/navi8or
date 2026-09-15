/* Substantially literal extraction of TDX src/core/hwind.c:adjust_area(). */
#include "nav_ui_core.h"

void nav_ui_adjust_area(NavUiAreaGeometry *area, int width, int height,
                       int row, int col, int screen_width, int screen_height,
                       const NavUiOutputContext *context)
{
    int shadow_width = 0;
    if (!area) return;
    if (context && context->output_space) {
        if (col + width < screen_width) ++width;
        if (col != 0) { --col; ++width; }
    }
    if (context && context->shadow) {
        shadow_width = screen_width - (col + width);
        if (shadow_width > context->shadow_width)
            shadow_width = context->shadow_width;
        width += shadow_width;
        if (row + height < screen_height) ++height;
    }
    area->col = col;
    area->row = row;
    area->width = width;
    area->height = height;
    area->shadow_width = shadow_width;
}
