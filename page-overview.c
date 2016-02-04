#include "page-overview.h"
#include "page-cache.h"
#include <stdio.h>

struct _PageOverview {
    gint *grid;           /* contains indices of pages */
    guint rows;
    guint columns;
    guint offset;
    guint current_row;
    guint current_column;
    guint page_count;
} page_overview;

void _page_overview_update_grid(void)
{
}

void page_overview_init(guint columns, guint rows)
{
    page_overview.rows = rows;
    page_overview.columns = columns;
    page_overview.page_count = 0;
    page_overview.grid = NULL;
    page_overview.current_row = 0;
    page_overview.current_column = 0;
}

void page_overview_cleanup(void)
{
    g_free(page_overview.grid);
}

gboolean _page_overview_is_pos_valid(guint col, guint row)
{
    if (page_overview.columns * row + col < page_overview.page_count)
        return TRUE;
    return FALSE;
}

/* do not allow navigation in both directions */
void page_overview_move(gint dx, gint dy)
{
    if (dy > 0) {
        /* FIXME: handle other increments than 1 */
        if (_page_overview_is_pos_valid(page_overview.current_column,
                                         page_overview.current_row + 1)) {
            ++page_overview.current_row;
#if 0
            /* FIXME: handle situation
             * { *  *  * }
             * { * [*]   } */
            page_overview.current_column = (page_overview.page_count - 1) / page_overview.columns
                + (page_overview.current_column <= page_overview.page_count % page_overview.columns ? 1 : 0);
#endif
        }
    }
    else if (dy < 0) {
        if (page_overview.current_row > 0)
            --page_overview.current_row;
    }
    else if (dx > 0) {
        if (page_overview.current_column < page_overview.columns - 1) {
            if (_page_overview_is_pos_valid(page_overview.current_column + 1, page_overview.current_row)) {
                ++page_overview.current_column;
            }
        }
        else {
            if (_page_overview_is_pos_valid(0, page_overview.current_row)) {
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

    /* FIXME: update offset */
}

gint page_overview_get_selection(guint *row, guint *column)
{
    if (row)
        *row = page_overview.current_row - page_overview.offset;
    if (column)
        *column = page_overview.current_column;

    guint pos = page_overview.current_row * page_overview.columns + page_overview.current_column;
    if (pos < page_overview.page_count)
        return page_overview.grid[pos];
    return -1;
}

void page_overview_set_page(gint index)
{
    /* find page belonging to index (or, if no match, page before that = first of group) */
}

void page_overview_update(void)
{
    g_free(page_overview.grid);
    page_overview.page_count = page_cache_get_page_count();
    page_overview.grid = g_malloc0(sizeof(gint) * page_overview.page_count);

    /* read list of indices */
}

