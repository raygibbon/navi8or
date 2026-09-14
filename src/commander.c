#include "nav.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

bool nav_entry_has_known_size(const NavEntry *entry)
{
    return entry && (entry->flags & NAV_ENTRY_SIZE_KNOWN) != 0;
}

void nav_provider_display_name(const NavProvider *provider, char *output,
                               size_t capacity)
{
    const char *scheme = provider && provider->scheme ? provider->scheme : "Provider";
    if (!output || capacity == 0) return;
    if (provider && provider->display_name && provider->display_name[0]) {
        snprintf(output, capacity, "%s", provider->display_name);
        return;
    }
    if (!strcasecmp(scheme, "local")) snprintf(output, capacity, "Local Filesystem");
    else {
        snprintf(output, capacity, "%s", scheme);
        if (output[0] >= 'a' && output[0] <= 'z') output[0] -= 'a' - 'A';
    }
}

void nav_provider_destroy(NavProvider *provider)
{
    if (provider && provider->destroy) provider->destroy(provider);
}

bool nav_provider_supports(const NavProvider *provider, unsigned capability)
{
    return provider && (provider->capabilities & capability) == capability;
}
bool nav_provider_resources_equal(const NavProvider *left, const char *left_id,
                                  const NavProvider *right, const char *right_id)
{
    return left && left == right && left_id && right_id && !strcmp(left_id, right_id);
}
NavPane *nav_active_pane(NavApp *app) { return &app->panes[app->active]; }
NavPane *nav_other_pane(NavApp *app) { return &app->panes[!app->active]; }

void nav_history_push(NavHistory *h, const NavLocation *location)
{
    if (h->current >= 0 && h->current < h->count &&
        h->locations[h->current].provider == location->provider &&
        !strcmp(h->locations[h->current].resource_id, location->resource_id))
        return;
    if (h->current + 1 < h->count)
        h->count = h->current + 1;
    int limit = h->limit > 0 && h->limit <= NAV_HISTORY_MAX ? h->limit : NAV_HISTORY_MAX;
    if (h->count >= limit)
    {
        memmove(h->locations, h->locations + 1, (size_t)(limit - 1) * sizeof h->locations[0]);
        --h->count;
    }
    h->locations[h->count] = *location;
    h->current = h->count++;
}
const NavLocation *nav_history_back(NavHistory *h) { return h->current > 0 ? &h->locations[--h->current] : NULL; }
const NavLocation *nav_history_forward(NavHistory *h) { return h->current + 1 < h->count ? &h->locations[++h->current] : NULL; }
int nav_pane_load(NavPane *p, const NavLocation *location, bool hidden, bool history, char *err, size_t en)
{
    if (!p->provider || !location || location->provider != p->provider ||
        !nav_provider_supports(p->provider, NAV_CAP_LIST))
    {
        snprintf(err, en, "provider cannot list this location");
        return -1;
    }
    if (p->provider->list(p->provider, location->resource_id, hidden, &p->listing, err, en))
        return -1;
    p->location = *location;
    p->selected = 0;
    p->offset = 0;
    if (history)
        nav_history_push(&p->history, &p->location);
    return 0;
}
int nav_pane_open(NavPane *p, const char *location, bool hidden, bool history, char *err, size_t en)
{
    NavLocation resolved;
    if (!p->provider || !p->provider->location ||
        p->provider->location(p->provider, location, &resolved, err, en))
        return -1;
    return nav_pane_load(p, &resolved, hidden, history, err, en);
}
int nav_pane_refresh(NavPane *p, bool hidden, char *err, size_t en)
{
    char selected[NAV_PATH_MAX] = {0};
    NavLocation location = p->location;
    const NavEntry *entry = nav_pane_selected(p);
    if (entry)
        snprintf(selected, sizeof selected, "%s", entry->resource_id);
    if (nav_pane_load(p, &location, hidden, false, err, en))
        return -1;
    if (selected[0])
    {
        int visible = 0;
        for (size_t index = 0; index < p->listing.count; index++)
            if (!p->filter[0] || strstr(p->listing.items[index].name, p->filter))
            {
                if (!strcmp(selected, p->listing.items[index].resource_id))
                {
                    p->selected = visible;
                    break;
                }
                visible++;
            }
    }
    nav_pane_ensure_visible(p);
    return 0;
}
static bool shown(const NavPane *p, const NavEntry *e) { return !p->filter[0] || strstr(e->name, p->filter) != NULL; }
int nav_pane_visible_count(const NavPane *p)
{
    int n = 0;
    for (size_t i = 0; i < p->listing.count; i++)
        if (shown(p, &p->listing.items[i]))
            n++;
    return n;
}
void nav_pane_clamp_selection(NavPane *p)
{
    int n = nav_pane_visible_count(p);
    if (n <= 0)
    {
        p->selected = 0;
        p->offset = 0;
        return;
    }
    if (p->selected < 0)
        p->selected = 0;
    if (p->selected >= n)
        p->selected = n - 1;
    if (p->offset < 0)
        p->offset = 0;
    if (p->offset >= n)
        p->offset = n - 1;
}
int nav_pane_page_capacity(const NavPane *p)
{
    int rows = p->rows_per_column > 0 ? p->rows_per_column : 1;
    int columns = p->view == NAV_PANEL_BRIEF && p->visible_columns > 0 ? p->visible_columns : 1;
    return rows * columns;
}
void nav_pane_ensure_visible(NavPane *p)
{
    int count, rows, capacity;
    nav_pane_clamp_selection(p);
    count = nav_pane_visible_count(p);
    capacity = nav_pane_page_capacity(p);
    if (capacity < 1 || count == 0)
        return;
    rows = p->rows_per_column > 0 ? p->rows_per_column : 1;
    if (p->view == NAV_PANEL_BRIEF)
    {
        int first_column = p->offset / rows;
        int selected_column = p->selected / rows;
        int total_columns = (count + rows - 1) / rows;
        int max_first = total_columns > p->visible_columns ? total_columns - p->visible_columns : 0;
        if (selected_column < first_column)
            first_column = selected_column;
        else if (selected_column >= first_column + p->visible_columns)
            first_column = selected_column - p->visible_columns + 1;
        if (first_column > max_first)
            first_column = max_first;
        if (first_column < 0)
            first_column = 0;
        p->offset = first_column * rows;
    }
    else if (p->selected < p->offset)
        p->offset = p->selected;
    else if (p->selected >= p->offset + capacity)
        p->offset = p->selected - capacity + 1;
}
void nav_pane_set_layout(NavPane *p, int width, int rows)
{
    p->rows_per_column = rows > 0 ? rows : 1;
    p->visible_columns = p->view == NAV_PANEL_BRIEF ? width / 28 : 1;
    if (p->visible_columns < 1) p->visible_columns = 1;
    nav_pane_ensure_visible(p);
}
void nav_entry_format_display_name(const NavEntry *entry, char *output, size_t capacity)
{
    if (!output || !capacity) return;
    snprintf(output, capacity, "%s%s", entry ? entry->name : "",
             entry && (entry->flags & NAV_ENTRY_DIR) ? "/" : "");
}
void nav_format_size(uint64_t bytes, char *output, size_t capacity)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = (double)bytes;
    size_t unit = 0;
    if (!output || capacity == 0) return;
    while (value >= 1024.0 && unit + 1 < sizeof units / sizeof *units) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0) snprintf(output, capacity, "%llu B", (unsigned long long)bytes);
    else {
        unsigned tenths = (unsigned)(value * 10.0 + 0.5);
        bool show_decimal = value < 10.0 || (value < 100.0 && tenths % 10 != 0);
        snprintf(output, capacity, show_decimal ? "%.1f %s" : "%.0f %s",
                 value, units[unit]);
    }
}
void nav_panel_full_layout(int width, NavPanelFullLayout *layout)
{
    /* One cell is the entry glyph and one is right-side breathing room. */
    int usable = width > 2 ? width - 2 : 1;
    memset(layout, 0, sizeof *layout);
    layout->name_width = usable;
    if (usable >= 28)
    {
        layout->show_size = true;
        layout->size_width = 12;
        layout->size_column = usable - layout->size_width;
        layout->name_width = layout->size_column - 2;
    }
    if (usable >= 54)
    {
        layout->show_modified = true;
        layout->modified_width = 16;
        layout->modified_column = usable - layout->modified_width;
        layout->size_column = layout->modified_column - 2 - layout->size_width;
        layout->name_width = layout->size_column - 2;
    }
    if (layout->name_width < 1) layout->name_width = 1;
}
static void full_layout_for_config(int width, NavPanelFullLayout *layout, const NavConfig *config)
{
    nav_panel_full_layout(width, layout);
    if (!config) return;
    int usable = width > 2 ? width - 2 : 1;
    layout->show_size = config->pane_show_size && usable >= 28;
    layout->show_modified = config->pane_show_modified && usable >= (layout->show_size ? 54 : 38);
    int end = usable;
    if (layout->show_modified) { layout->modified_width = 16; layout->modified_column = end - 16; end -= 18; }
    if (layout->show_size) { layout->size_width = 12; layout->size_column = end - 12; end -= 14; }
    layout->name_width = end > 0 ? end : 1;
}
static void put_header_label(char *row, int row_width, int column,
                             int field_width, const char *label,
                             bool align_right)
{
    int field_end, label_length, amount, destination, source = 0;
    if (!row || !label || column < 0 || column >= row_width || field_width <= 0)
        return;
    field_end = column + field_width;
    if (field_end > row_width) field_end = row_width;
    label_length = (int)strlen(label);
    amount = label_length;
    if (amount > field_end - column) amount = field_end - column;
    destination = column;
    if (align_right) {
        destination = field_end - amount;
        source = label_length - amount;
    }
    if (amount > 0) memcpy(row + destination, label + source, (size_t)amount);
}
void nav_format_panel_header_for_config(NavPanelView view, NavSortMode sort, int width,
                             char *output, size_t capacity, const NavConfig *config)
{
    NavPanelFullLayout layout;
    int text_width = width > 2 ? width - 2 : 1;
    if (!output || capacity == 0) return;
    if (text_width >= (int)capacity) text_width = (int)capacity - 1;
    if (text_width < 0) text_width = 0;
    full_layout_for_config(width, &layout, config);
    memset(output, ' ', (size_t)text_width);
    output[text_width] = 0;
    if (text_width == 0) return;
    put_header_label(output, text_width, 0,
                     view == NAV_PANEL_BRIEF ? text_width : layout.name_width,
                     sort == NAV_SORT_NAME ? "Name ^" : "Name", false);
    if (view == NAV_PANEL_FULL && layout.show_size)
        put_header_label(output, text_width, layout.size_column,
                         layout.size_width,
                         sort == NAV_SORT_SIZE ? "Size ^" : "Size", true);
    if (view == NAV_PANEL_FULL && layout.show_modified)
        put_header_label(output, text_width, layout.modified_column,
                         layout.modified_width,
                         sort == NAV_SORT_DATE ? "Modified ^" : "Modified",
                         false);
}
void nav_format_entry_full_for_config(const NavEntry *entry, int width, char *output,
                           size_t capacity, const NavConfig *config)
{
    NavPanelFullLayout layout;
    char display[NAV_NAME_MAX + 2], size[32] = "-", modified[32] = "-";
    int text_width = width > 2 ? width - 2 : 1;
    if (!output || capacity == 0) return;
    if (text_width >= (int)capacity) text_width = (int)capacity - 1;
    if (text_width < 0) text_width = 0;
    full_layout_for_config(width, &layout, config);
    memset(output, ' ', (size_t)text_width);
    output[text_width] = 0;
    if (!entry || text_width == 0) return;
    nav_entry_format_display_name(entry, display, sizeof display);
    memcpy(output, display, strnlen(display,
           (size_t)(layout.name_width < text_width ? layout.name_width : text_width)));
    if (layout.show_size && layout.size_column < text_width) {
        int end = layout.size_column + layout.size_width;
        int length, start;
        if (entry->flags & NAV_ENTRY_DIR) snprintf(size, sizeof size, "<DIR>");
        else if (entry->flags & NAV_ENTRY_SIZE_KNOWN) {
            if (config && config->size_bytes) snprintf(size, sizeof size, "%llu", (unsigned long long)entry->size);
            else nav_format_size(entry->size, size, sizeof size);
        }
        if (end > text_width) end = text_width;
        length = (int)strlen(size);
        if (length > end - layout.size_column) length = end - layout.size_column;
        start = end - length;
        if (length > 0) memcpy(output + start, size, (size_t)length);
    }
    if (layout.show_modified && layout.modified_column < text_width) {
        int amount;
        if (entry->flags & NAV_ENTRY_MODIFIED_KNOWN) {
            struct tm value;
            nav_platform_localtime(&entry->modified, &value);
            strftime(modified, sizeof modified, config ? config->date_format : "%Y-%m-%d %H:%M", &value);
        }
        amount = (int)strlen(modified);
        if (amount > layout.modified_width) amount = layout.modified_width;
        if (amount > text_width - layout.modified_column) amount = text_width - layout.modified_column;
        if (amount > 0) memcpy(output + layout.modified_column, modified, (size_t)amount);
    }
}
void nav_pane_move(NavPane *p, int delta)
{
    p->selected += delta;
    nav_pane_clamp_selection(p);
    nav_pane_ensure_visible(p);
}
void nav_pane_page(NavPane *p, int direction)
{
    int page = nav_pane_page_capacity(p);
    nav_pane_move(p, direction * (page > 0 ? page : 1));
}
const NavEntry *nav_pane_selected(const NavPane *p)
{
    int n = 0;
    for (size_t i = 0; i < p->listing.count; i++)
        if (shown(p, &p->listing.items[i]) && n++ == p->selected)
            return &p->listing.items[i];
    return NULL;
}
static int compare(const NavEntry *a, const NavEntry *b, const NavPane *pane)
{
    if (a->flags & NAV_ENTRY_PARENT)
        return -1;
    if (b->flags & NAV_ENTRY_PARENT)
        return 1;
    if (pane->directories_first && !!(a->flags & NAV_ENTRY_DIR) != !!(b->flags & NAV_ENTRY_DIR))
        return (a->flags & NAV_ENTRY_DIR) ? -1 : 1;
    if (pane->sort_mode == NAV_SORT_SIZE && a->size != b->size)
        return a->size < b->size ? -1 : 1;
    if (pane->sort_mode == NAV_SORT_DATE && a->modified != b->modified)
        return a->modified > b->modified ? -1 : 1;
    return pane->case_sensitive_sort ? strcmp(a->name, b->name) : strcasecmp(a->name, b->name);
}
void nav_pane_sort(NavPane *p, NavSortMode mode)
{
    char selected[NAV_PATH_MAX] = {0};
    const NavEntry *current = nav_pane_selected(p);
    if (current)
        snprintf(selected, sizeof selected, "%s", current->resource_id);
    p->sort_mode = mode;
    for (size_t i = 1; i < p->listing.count; i++)
    {
        NavEntry item = p->listing.items[i];
        size_t j = i;
        while (j > 0 && compare(&item, &p->listing.items[j - 1], p) < 0)
        {
            p->listing.items[j] = p->listing.items[j - 1];
            j--;
        }
        p->listing.items[j] = item;
    }
    p->selected = 0;
    if (selected[0])
    {
        int visible = 0;
        for (size_t i = 0; i < p->listing.count; i++)
            if (shown(p, &p->listing.items[i]))
            {
                if (!strcmp(selected, p->listing.items[i].resource_id))
                {
                    p->selected = visible;
                    break;
                }
                visible++;
            }
    }
    nav_pane_clamp_selection(p);
}

void nav_format_panel_header(NavPanelView view, NavSortMode sort, int width, char *out, size_t size)
{ nav_format_panel_header_for_config(view, sort, width, out, size, NULL); }
void nav_format_entry_full(const NavEntry *entry, int width, char *out, size_t size)
{ nav_format_entry_full_for_config(entry, width, out, size, NULL); }
