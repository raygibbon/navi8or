/* Navi8or UI conveniences over the terminal and core controls. */
#include "nav_ui.h"
#include "nav_ui_core.h"
#include <stdio.h>
#include <string.h>

void nav_ui_text(int x, int y, int width, const char *text, NavStyle style)
{
    if (width > 0)
        nav_term_text(x, y, width, text, style);
}

void nav_ui_box(int x, int y, int width, int height, const char *title, NavStyle style)
{
    nav_ui_frame(x, y, width, height, NULL, 0, title, style);
}

void nav_show_properties(const NavEntry *entry, const char *type)
{
    char name[320], path[NAV_PATH_MAX + 32], size[128], modified[128], timebuf[64];
    struct tm tm_value;
    if (!entry)
        return;
    if (entry->flags & NAV_ENTRY_MODIFIED_KNOWN) {
        localtime_r(&entry->modified, &tm_value);
        strftime(timebuf, sizeof timebuf, "%Y-%m-%d %H:%M", &tm_value);
    } else snprintf(timebuf, sizeof timebuf, "Unknown");
    snprintf(name, sizeof name, "Name:      %s", entry->name);
    snprintf(path, sizeof path, "Path:      %s", entry->resource_id);
    if (entry->flags & NAV_ENTRY_SIZE_KNOWN)
        snprintf(size, sizeof size, "Size:      %llu bytes", (unsigned long long)entry->size);
    else snprintf(size, sizeof size, "Size:      Unknown");
    snprintf(modified, sizeof modified, "Modified:  %s", timebuf);
    {
        const char *lines[] = {name, path, type ? type : "Type:      Unknown", size, modified};
        nav_ui_info(" File Properties ", lines, sizeof lines / sizeof *lines);
    }
}
