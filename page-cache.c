#include "page-cache.h"
#include <memory.h>
#include "utils.h"
#include <cairo.h>

struct _Page {
  unsigned int width;
  unsigned int height;
  cairo_surface_t *surf;
  unsigned char *compressed_buffer;
  GMutex *data_lock;
  unsigned int ref_count;
  unsigned int split_guess : 1;
  unsigned int compressed : 1;
  unsigned int uncompressed : 1;
};

struct _PageCache {
  PopplerDocument *doc;
  GMutex *control_lock;
  unsigned int pages_cached;
  unsigned int npages;
  unsigned int current_index;
  double scale_to_height;
  struct _Page *pages;
  GList *page_links;
  double current_scale;
} _page_cache;

struct _Page * _page_cache_get_page(int index);

int page_cache_load_document(const gchar *uri, double scale_to_height) {
  unsigned int i;
  gchar *_uri;
  memset(&_page_cache, 0, sizeof(struct _PageCache));
  if (!uri) {
    return 1;
  }
  _uri = util_make_uri(uri);
  _page_cache.doc = poppler_document_new_from_file(_uri, NULL, NULL);
  g_free(_uri);

  if (!_page_cache.doc) {
    return 1;
  }

  _page_cache.current_index = 0;
  _page_cache.scale_to_height = scale_to_height;
  _page_cache.npages = poppler_document_get_n_pages(_page_cache.doc);
  _page_cache.control_lock = g_mutex_new();

  _page_cache.pages = g_malloc0(sizeof(struct _Page)*_page_cache.npages);
  for (i = 0; i < _page_cache.npages; i++) {
    _page_cache.pages[i].data_lock = g_mutex_new();
  }
  return 0;
}

void page_cache_unload_document(void) {
  unsigned int i;
  for (i = 0; i < _page_cache.npages && _page_cache.pages; i++) {
    if (_page_cache.pages[i].data_lock)
      g_mutex_free(_page_cache.pages[i].data_lock);
    if (_page_cache.pages[i].surf)
      cairo_surface_destroy(_page_cache.pages[i].surf);
    if (_page_cache.pages[i].compressed_buffer)
      g_free(_page_cache.pages[i].compressed_buffer);
  }
  g_free(_page_cache.pages);
  if (_page_cache.control_lock)
    g_mutex_free(_page_cache.control_lock);
  if (_page_cache.doc)
    g_object_unref(_page_cache.doc);
}

unsigned int page_cache_get_page_count(void) {
  return _page_cache.npages;
}

int page_cache_load_page(int index) {
  PopplerPage *page;
  double h;
  unsigned int ph;
  if (page_cache_fetch_page(index, NULL, NULL, &ph, NULL) != 0) {
    return 1;
  }
  page_cache_page_reference(index);
  /* load links */
  if (_page_cache.page_links) {
    poppler_page_free_link_mapping(_page_cache.page_links);
    _page_cache.page_links = NULL;
  }
  page = poppler_document_get_page(_page_cache.doc, index);
  if (page) {
    _page_cache.page_links = poppler_page_get_link_mapping(page);
    poppler_page_get_size(page, NULL, &h);
    _page_cache.current_scale = ph/h;
    g_object_unref(page);
  }

  return 0;
}

int page_cache_fetch_page(int index, cairo_surface_t **surf, unsigned int *width, unsigned int *height, int *guess_split) {
  struct _Page *pg = _page_cache_get_page(index);
  double scale, w, h;
  PopplerPage *page;
  cairo_t *c;
  if (!pg) {
    return 1;
  }
  /* if surface exists (and is set) get surface */
  /* else if compressed exists, uncompress, get surface */
  /* else init width, height, surface (render) */
  if (pg->uncompressed && pg->surf) {
    if (surf) *surf = pg->surf;
    if (width) *width = pg->width;
    if (height) *height = pg->height;
    if (guess_split) *guess_split = pg->split_guess;
  }
  else if (pg->compressed && pg->compressed_buffer) {
  }
  else {
    page = poppler_document_get_page(_page_cache.doc, index);
    if (!page) {
      return 1;
    }
    poppler_page_get_size(page, &w, &h);
    scale = _page_cache.scale_to_height / h;
    pg->width = (unsigned int)(scale * w);
    pg->height = (unsigned int)(scale * h);
    if (pg->width > 2*pg->height) {
      pg->split_guess = 1;
    }
    else {
      pg->split_guess = 0;
    }
    pg->surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)(scale * w), (int)(scale * h));
    if (!pg->surf) {
      g_object_unref(page);
      return 1;
    }
    c = cairo_create(pg->surf);

    cairo_scale(c, scale, scale);
    
    cairo_set_source_rgb(c, 1.0f, 1.0f, 1.0f);
    cairo_rectangle(c, 0, 0, w, h);
    cairo_fill(c);

    poppler_page_render(page, c);

    cairo_destroy(c);
    g_object_unref(page);

    pg->uncompressed = 1;

    if (surf) *surf = pg->surf;
    if (width) *width = pg->width;
    if (height) *height = pg->height;
    if (guess_split) *guess_split = pg->split_guess;
  }
  return 0;
}

void page_cache_page_reference(int index) {
  struct _Page *pg = _page_cache_get_page(index);
  if (pg) {
    pg->ref_count++;
  }
}

void page_cache_page_unref(int index) {
  struct _Page *pg = _page_cache_get_page(index);
  if (pg && pg->ref_count) {
    pg->ref_count--;
  }
  if (pg->ref_count == 0) {
    if (pg->surf) {
      cairo_surface_destroy(pg->surf);
      pg->surf = NULL;
    }
    pg->uncompressed = 0;
  }
}

PopplerAction *page_cache_get_action_from_pos(double x, double y) {
  GList *tmp = _page_cache.page_links;
  UtilPoint pt = { x / _page_cache.current_scale, 
                   y / _page_cache.current_scale };
  UtilRect r;

  while (tmp) {
    r.x1 = ((PopplerLinkMapping*)tmp->data)->area.x1;
    r.x2 = ((PopplerLinkMapping*)tmp->data)->area.x2;
    r.y1 = ((PopplerLinkMapping*)tmp->data)->area.y1;
    r.y2 = ((PopplerLinkMapping*)tmp->data)->area.y2;
    if (util_point_in_rect(&pt, &r)) {
      return ((PopplerLinkMapping*)tmp->data)->action;
    }
    tmp = tmp->next;
  }
  return NULL;
}

PopplerDest *page_cache_get_named_dest(const gchar *dest) {
  return poppler_document_find_dest(_page_cache.doc, dest);
}

struct _Page * _page_cache_get_page(int index) {
  if (index < 0 || index >= _page_cache.npages) {
    return NULL;
  }
  return &_page_cache.pages[index];
}


