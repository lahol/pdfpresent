#include "page-overview.h"
#include "page-cache.h"
#include <stdio.h>

struct _PageOverviewLabelIndex {
    gchar *label;
    gint index;
};

struct _PageOverview {
    struct _PageOverviewLabelIndex *grid;           /* contains indices of pages */
    guint rows;
    guint columns;
    guint offset;
    guint current_row;
    guint current_column;
    guint page_count;
    guint display_rows;
} page_overview;

void _page_overview_free_grid(void)
{
    guint i;
    for (i = 0; i < page_overview.page_count; ++i) {
        g_free(page_overview.grid[i].label);
    }
    g_free(page_overview.grid);
    page_overview.grid = NULL;
}

void page_overview_init(guint columns)
{
    page_overview.rows = 0;
    page_overview.columns = columns;
    page_overview.page_count = 0;
    page_overview.grid = NULL;
    page_overview.current_row = 0;
    page_overview.current_column = 0;
    page_overview.display_rows = 1;
}

void _page_overview_update_offset(void)
{
    if (page_overview.current_row < page_overview.offset)
        page_overview.offset = page_overview.current_row;
    else if (page_overview.current_row >= page_overview.offset + page_overview.display_rows)
        page_overview.offset = page_overview.current_row - page_overview.display_rows + 1;
}

void page_overview_set_display_rows(guint display_rows)
{
    page_overview.display_rows = display_rows;
    _page_overview_update_offset();
}

void page_overview_cleanup(void)
{
    g_free(page_overview.grid);
}

gint _page_overview_is_pos_valid(guint col, guint row)
{
    gint pos = page_overview.columns * row + col;
    if (pos < page_overview.page_count)
        return pos;
    return -1;
}

/* do not allow navigation in both directions */
void page_overview_move(gint dx, gint dy)
{
    if (dy > 0) {
        if (page_overview.current_row + dy >= page_overview.rows)
            page_overview.current_row = page_overview.rows - 1;
        else
            page_overview.current_row += dy;

        if (_page_overview_is_pos_valid(page_overview.current_column, page_overview.current_row) < 0 &&
                page_overview.current_row > 0)
            --page_overview.current_row;
    }
    else if (dy < 0) {
        if (page_overview.current_row < -dy)
            page_overview.current_row = 0;
        else
            page_overview.current_row += dy;
    }
    else if (dx > 0) {
        if (page_overview.current_column < page_overview.columns - 1) {
            if (_page_overview_is_pos_valid(page_overview.current_column + 1, page_overview.current_row) >= 0) {
                ++page_overview.current_column;
            }
        }
        else {
            if (_page_overview_is_pos_valid(0, page_overview.current_row) >= 0) {
                page_overview.current_column = 0;
                ++page_overview.current_row;
            }
        }
    }
    else if (dx < 0) {
        if (page_overview.current_column > 0)
            --page_overview.current_column;
        else if (page_overview.current_row > 0) {
            --page_overview.current_row;
            page_overview.current_column = page_overview.columns - 1;
        }
    }

    _page_overview_update_offset();
}

gint page_overview_get_selection(guint *row, guint *column)
{
    if (row)
        *row = page_overview.current_row - page_overview.offset;
    if (column)
        *column = page_overview.current_column;

    guint pos = page_overview.current_row * page_overview.columns + page_overview.current_column;
    if (pos < page_overview.page_count)
        return page_overview.grid[pos].index;
    return -1;
}

gboolean page_overview_get_page(guint row, guint column, gint *index, gchar **label)
{
    gint pos;
    if ((pos = _page_overview_is_pos_valid(column, row + page_overview.offset)) < 0)
        return FALSE;

    if (index)
        *index = page_overview.grid[pos].index;
    if (label)
        *label = page_overview.grid[pos].label;

    return TRUE;
}

void page_overview_set_page(gint index)
{
    /* find page belonging to index (or, if no match, page before that = first of group) */
}

void page_overview_enum_labels_cb(gchar *label, gint index, GList **list)
{
    ++page_overview.page_count;
    struct _PageOverviewLabelIndex *label_index = g_malloc(sizeof(struct _PageOverviewLabelIndex));
    label_index->label = g_strdup(label);
    label_index->index = index;

    *list = g_list_prepend(*list, label_index);
}

void page_overview_update(void)
{
    _page_overview_free_grid();

    page_overview.page_count = 0;
    GList *label_list = NULL;

    page_cache_enum_labels((PageCacheEnumLabelsProc)page_overview_enum_labels_cb, &label_list);

    page_overview.grid = g_malloc0(sizeof(struct _PageOverviewLabelIndex) * page_overview.page_count);

    GList *tmp;
    guint i;
    for (i = 0, tmp = g_list_last(label_list);
         tmp && i < page_overview.page_count;
         ++i, tmp = g_list_previous(tmp)) {
        page_overview.grid[i] = *((struct _PageOverviewLabelIndex *)tmp->data);
    }

    /* only delete structures, not content, which is now in array */
    g_list_free_full(label_list, (GDestroyNotify)g_free);

    page_overview.rows = (page_overview.page_count + page_overview.columns - 1) / page_overview.columns;
}

void page_overview_get_grid_size(guint *rows, guint *columns)
{
    if (rows)
        *rows = page_overview.rows;
    if (columns)
        *columns = page_overview.columns;
}

guint page_overview_get_offset(void)
{
    return page_overview.offset;
}
