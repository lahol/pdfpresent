#ifndef __PAGE_OVERVIEW_H__
#define __PAGE_OVERVIEW_H__

#include <glib.h>

void page_overview_init(guint columns);
void page_overview_cleanup(void);
void page_overview_set_display_rows(guint display_rows);
void page_overview_move(gint dx, gint dy);
void page_overview_scroll(gint dy);
gint page_overview_get_selection(guint *row, guint *column);
gboolean page_overview_get_page(guint row, guint column, gint *index, gchar **label, gboolean absolute);
void page_overview_get_grid_size(guint *rows, guint *columns);
guint page_overview_get_offset(void);

void page_overview_set_page(gint index);
void page_overview_update(void);

#endif
