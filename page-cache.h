#ifndef __PAGE_CACHE_H__
#define __PAGE_CACHE_H__

#include <glib.h>
#include <cairo.h>
#include <poppler.h>

typedef struct _PageCacheStatus {
    unsigned int pages_cached;
    unsigned int page_count;
    gsize cached_size;
} PageCacheStatus;

int page_cache_init(void);
void page_cache_cleanup(void);
int page_cache_load_document(const gchar *uri);
void page_cache_unload_document(void);

void page_cache_set_scale_to_height(double scale_to_height);

unsigned int page_cache_get_page_count(void);
void page_cache_get_status(PageCacheStatus *status);
void page_cache_start_caching(void);
void page_cache_stop_caching(void);
int page_cache_load_page(int index);
int page_cache_fetch_page(int index, cairo_surface_t **surf, unsigned int *width, unsigned int *height, int *guess_split);
void page_cache_page_reference(int index);
void page_cache_page_unref(int index);
PopplerAction *page_cache_get_action_from_pos(double x, double y);
PopplerDest *page_cache_get_named_dest(const gchar *dest);

/* label, index of first page, userdata */
typedef void (*PageCacheEnumLabelsProc)(gchar *, gint, gpointer);
void page_cache_enum_labels(PageCacheEnumLabelsProc callback, gpointer userdata);

/* get action rects for page */

#endif
