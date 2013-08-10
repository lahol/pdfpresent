#include "page-cache.h"
#include <memory.h>
#include "utils.h"
#include <cairo.h>
#include <glib.h>
#include <zlib.h>
#include <stdio.h>

#define PAGE_STATE_CREATING_SURFACE      1
#define PAGE_STATE_COMPRESSING           2
#define PAGE_STATE_UNCOMPRESSING         4
#define PAGE_STATE_READY                 8

struct _Page {
    unsigned int width;
    unsigned int height;
    cairo_surface_t *surf;
    unsigned char *compressed_buffer;
    unsigned char *uncompressed_buffer;
    gsize buffer_size;
    GMutex page_lock;
    unsigned int ref_count;
    unsigned int split_guess : 1;
    unsigned int compressed : 1;
    unsigned int uncompressed : 1;
};

struct _PageCache {
    PopplerDocument *doc;
    GMutex control_lock;
    GMutex data_lock;
    GMutex poppler_lock;
    GThread *cache_thread;
    unsigned int pages_cached;
    unsigned int npages;
    unsigned int current_index;
    double scale_to_height;
    struct _Page *pages;
    GList *page_links;
    double current_scale;
    int do_caching;
} _page_cache;

struct _Page *_page_cache_get_page(int index);
int _page_cache_render_page(int index, cairo_surface_t **surf, unsigned int *width, unsigned int *height);
int _page_cache_compress_page(int index);
int _page_cache_uncompress_page(int index);
int _page_cache_compress_buffer(unsigned char *in, gsize insize, unsigned char **out, gsize *outsize);
int _page_cache_uncompress_buffer(unsigned char *in, gsize insize, unsigned char **out, gsize outsize);

int page_cache_load_document(const gchar *uri, double scale_to_height)
{
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
    g_mutex_init(&_page_cache.control_lock);
    g_mutex_init(&_page_cache.data_lock);
    g_mutex_init(&_page_cache.poppler_lock);

    _page_cache.pages = g_malloc0(sizeof(struct _Page)*_page_cache.npages);
    for (i = 0; i < _page_cache.npages; i++) {
        g_mutex_init(&_page_cache.pages[i].page_lock);
    }
    return 0;
}

void page_cache_unload_document(void)
{
    unsigned int i;
    for (i = 0; i < _page_cache.npages && _page_cache.pages; i++) {
        g_mutex_clear(&_page_cache.pages[i].page_lock);
        if (_page_cache.pages[i].surf)
            cairo_surface_destroy(_page_cache.pages[i].surf);
        if (_page_cache.pages[i].compressed_buffer)
            g_free(_page_cache.pages[i].compressed_buffer);
        if (_page_cache.pages[i].uncompressed_buffer)
            g_free(_page_cache.pages[i].uncompressed_buffer);
    }
    g_free(_page_cache.pages);

    g_mutex_clear(&_page_cache.control_lock);
    g_mutex_clear(&_page_cache.data_lock);
    g_mutex_clear(&_page_cache.poppler_lock);

    if (_page_cache.doc)
        g_object_unref(_page_cache.doc);
}

unsigned int page_cache_get_page_count(void)
{
    return _page_cache.npages;
}

void page_cache_get_status(PageCacheStatus *status)
{
    unsigned int i;
    if (status) {
        status->pages_cached = _page_cache.pages_cached;
        status->page_count = _page_cache.npages;
        status->cached_size = 0;
        for (i = 0; i < status->page_count; i++) {
            if (g_mutex_trylock(&_page_cache.pages[i].page_lock)) {
                status->cached_size += _page_cache.pages[i].buffer_size;
                g_mutex_unlock(&_page_cache.pages[i].page_lock);
            }
        }
    }
}

gpointer _page_cache_caching_thread(gpointer data)
{
    unsigned int i;
    int do_caching = 1;
    struct _Page *pg = NULL;
    int next;
    while (1) {
        g_mutex_lock(&_page_cache.control_lock);
        i = _page_cache.current_index + 1;
        if (_page_cache.pages_cached == _page_cache.npages)
            _page_cache.do_caching = 0;
        do_caching = _page_cache.do_caching;
        g_mutex_unlock(&_page_cache.control_lock);
        if (!do_caching)
            break;
        next = 0;
        while (!next) {
            if (i == _page_cache.npages) {
                i = 0;
            }
            pg = _page_cache_get_page(i);
            if (pg) {
                g_mutex_lock(&pg->page_lock);
                if (!pg->compressed)
                    next = 1;
                g_mutex_unlock(&pg->page_lock);
            }
            if (!next) {
                if (i == _page_cache.current_index)
                    break;
                i++;
            }
        }
        if (next) {
            pg = _page_cache_get_page(i);
            if (pg) {
                g_mutex_lock(&pg->page_lock);
                if (_page_cache_compress_page(i) == 0) {
                    g_mutex_lock(&_page_cache.control_lock);
                    _page_cache.pages_cached++;
                    g_mutex_unlock(&_page_cache.control_lock);
                }
                g_mutex_unlock(&pg->page_lock);
            }
        }
    }
    return NULL;
}

void page_cache_start_caching(void)
{
    _page_cache.do_caching = 1;
    _page_cache.cache_thread =
        g_thread_new("PageCache", _page_cache_caching_thread, NULL);
}

void page_cache_stop_caching(void)
{
    g_mutex_lock(&_page_cache.control_lock);
    _page_cache.do_caching = 0;
    g_mutex_unlock(&_page_cache.control_lock);
    if (_page_cache.cache_thread)
        g_thread_join(_page_cache.cache_thread);
}

int page_cache_load_page(int index)
{
    PopplerPage *page;
    double h;
    unsigned int ph;
    if (page_cache_fetch_page(index, NULL, NULL, &ph, NULL) != 0) {
        return 1;
    }
    page_cache_page_reference(index);
    /* load links */
    g_mutex_lock(&_page_cache.data_lock);
    g_mutex_lock(&_page_cache.poppler_lock);
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
    g_mutex_unlock(&_page_cache.poppler_lock);
    g_mutex_lock(&_page_cache.control_lock);
    _page_cache.current_index = index;
    g_mutex_unlock(&_page_cache.control_lock);
    g_mutex_unlock(&_page_cache.data_lock);

    return 0;
}

int page_cache_fetch_page(int index, cairo_surface_t **surf, unsigned int *width, unsigned int *height, int *guess_split)
{
    struct _Page *pg = _page_cache_get_page(index);
    if (!pg) {
        return 1;
    }
    /* if surface exists (and is set) get surface */
    /* else if compressed exists, uncompress, get surface */
    /* else init width, height, surface (render) */
    g_mutex_lock(&pg->page_lock);
    if (pg->uncompressed && pg->surf) {
        if (surf) *surf = pg->surf;
        if (width) *width = pg->width;
        if (height) *height = pg->height;
    }
    else if (pg->compressed && pg->compressed_buffer) {
        if (_page_cache_uncompress_page(index) != 0) {
            g_mutex_unlock(&pg->page_lock);
            fprintf(stderr, "could not uncompress page\n");
            return 1;
        }
        if (surf) *surf = pg->surf;
        if (width) *width = pg->width;
        if (height) *height = pg->height;
    }
    else {
        if (_page_cache_render_page(index, &pg->surf, &pg->width, &pg->height) != 0) {
            g_mutex_unlock(&pg->page_lock);
            fprintf(stderr, "render page return non null\n");
            return 1;
        }
        if (pg->width > 2*pg->height) {
            pg->split_guess = 1;
        }
        else {
            pg->split_guess = 0;
        }

        pg->uncompressed = 1;

        if (surf) *surf = pg->surf;
        if (width) *width = pg->width;
        if (height) *height = pg->height;
    }

    pg->split_guess = (pg->width > 2*pg->height ? 1 : 0);
    if (guess_split) *guess_split = pg->split_guess;

    g_mutex_unlock(&pg->page_lock);
    return 0;
}

void page_cache_page_reference(int index)
{
    struct _Page *pg = _page_cache_get_page(index);
    if (pg) {
        g_mutex_lock(&pg->page_lock);
        pg->ref_count++;
        g_mutex_unlock(&pg->page_lock);
    }
}

void page_cache_page_unref(int index)
{
    struct _Page *pg = _page_cache_get_page(index);
    if (pg) {
        g_mutex_lock(&pg->page_lock);
        if (pg->ref_count) {
            pg->ref_count--;
        }
        if (pg->ref_count == 0) {
            if (pg->surf) {
                cairo_surface_destroy(pg->surf);
                pg->surf = NULL;
            }
            if (pg->uncompressed_buffer) {
                g_free(pg->uncompressed_buffer);
                pg->uncompressed_buffer = NULL;
            }
            pg->uncompressed = 0;
        }
        g_mutex_unlock(&pg->page_lock);
    }
}

PopplerAction *page_cache_get_action_from_pos(double x, double y)
{
    g_mutex_lock(&_page_cache.data_lock);
    g_mutex_lock(&_page_cache.poppler_lock);
    PopplerAction *ret = NULL;
    GList *tmp = _page_cache.page_links;
    UtilPoint pt = { x / _page_cache.current_scale,
                     y / _page_cache.current_scale
                   };
    UtilRect r;

    while (tmp) {
        r.x1 = ((PopplerLinkMapping *)tmp->data)->area.x1;
        r.x2 = ((PopplerLinkMapping *)tmp->data)->area.x2;
        r.y1 = ((PopplerLinkMapping *)tmp->data)->area.y1;
        r.y2 = ((PopplerLinkMapping *)tmp->data)->area.y2;
        if (util_point_in_rect(&pt, &r)) {
            ret = ((PopplerLinkMapping *)tmp->data)->action;
            g_mutex_unlock(&_page_cache.poppler_lock);
            g_mutex_unlock(&_page_cache.data_lock);
            return ret;
        }
        tmp = tmp->next;
    }
    g_mutex_unlock(&_page_cache.poppler_lock);
    g_mutex_unlock(&_page_cache.data_lock);
    return NULL;
}

PopplerDest *page_cache_get_named_dest(const gchar *dest)
{
    g_mutex_lock(&_page_cache.poppler_lock);
    PopplerDest *d = poppler_document_find_dest(_page_cache.doc, dest);
    g_mutex_unlock(&_page_cache.poppler_lock);
    return d;
}

struct _Page *_page_cache_get_page(int index)
{
    if (index < 0 || index >= _page_cache.npages) {
        return NULL;
    }
    return &_page_cache.pages[index];
}

int _page_cache_render_page(int index, cairo_surface_t **surf, unsigned int *width, unsigned int *height)
{
    PopplerPage *page;
    unsigned int w, h;
    double ph, pw;
    double scale;
    cairo_t *c;
    /*  PopplerRectangle cropbox;*/
    if (!surf) return 1;

    g_mutex_lock(&_page_cache.poppler_lock);
    page = poppler_document_get_page(_page_cache.doc, index);
    if (!page) {
        g_mutex_unlock(&_page_cache.poppler_lock);
        return 1;
    }
    poppler_page_get_size(page, &pw, &ph);
    /* cut of one inch, did not affect working pdfs but fixed wrong margin on some tex-a4paper-pdf */
    scale = _page_cache.scale_to_height / (ph-72);

    /*  poppler_page_get_crop_box(page, &cropbox);
      fprintf(stderr, "cropbox: %f, %f, %f, %f\n", cropbox.x1, cropbox.y1, cropbox.x2, cropbox.y2);*/

    w = (unsigned int)(scale * pw + 0.75f);
    h = (unsigned int)(scale * ph + 0.75f);

    *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)w, (int)h);
    if (!(*surf)) {
        g_object_unref(page);
        g_mutex_unlock(&_page_cache.poppler_lock);
        return 1;
    }

    c = cairo_create(*surf);
    if (!c) {
        g_object_unref(page);
        g_mutex_unlock(&_page_cache.poppler_lock);
        return 1;
    }

    cairo_scale(c, scale, scale);

    cairo_set_source_rgb(c, 1.0f, 1.0f, 1.0f);
    cairo_rectangle(c, 0, 0, w, h);
    cairo_fill(c);

    poppler_page_render(page, c);

    cairo_destroy(c);
    g_object_unref(page);

    g_mutex_unlock(&_page_cache.poppler_lock);

    if (width) *width = w;
    if (height) *height = h;

    return 0;
}

int _page_cache_compress_page(int index)
{
    cairo_surface_t *pgsurf = NULL;
    unsigned char *buffer = NULL;
    unsigned int width, height, stride;
    gsize bufsize;
    struct _Page *pg = _page_cache_get_page(index);
    if (!pg)
        return 1;
    if (_page_cache_render_page(index, &pgsurf, &width, &height) != 0) {
        return 1;
    }
    stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    bufsize = stride * height;

    buffer = cairo_image_surface_get_data(pgsurf);
    if (buffer) {
        if (_page_cache_compress_buffer(buffer, bufsize, &pg->compressed_buffer, &pg->buffer_size) != 0) {
            return 1;
        }
        pg->compressed = 1;
        pg->width = width;
        pg->height = height;
    }

    cairo_surface_destroy(pgsurf);

    return 0;
}

int _page_cache_uncompress_page(int index)
{
    struct _Page *pg = _page_cache_get_page(index);
    gsize bufsize;
    unsigned int stride;
    if (!pg)
        return 1;
    stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pg->width);
    bufsize = pg->height*stride;
    if (_page_cache_uncompress_buffer(pg->compressed_buffer, pg->buffer_size, &pg->uncompressed_buffer, bufsize) != 0) {
        return 1;
    }
    if (pg->uncompressed_buffer) {
        pg->surf = cairo_image_surface_create_for_data(pg->uncompressed_buffer,
                   CAIRO_FORMAT_ARGB32, pg->width, pg->height, stride);
        pg->uncompressed = 1;
    }
    else {
        return 1;
    }
    return 0;
}

int _page_cache_compress_buffer(unsigned char *in, gsize insize, unsigned char **out, gsize *outsize)
{
    int ret;
    uLong bound;
    unsigned char *obuf;
    z_stream strm;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, 6);
    if (ret != Z_OK)
        return 1;
    bound = deflateBound(&strm, insize);

    obuf = g_malloc(bound);
    strm.avail_in = insize;
    strm.next_in = in;
    strm.avail_out = bound;
    strm.next_out = obuf;
    ret = deflate(&strm, Z_FINISH);
    if (ret == Z_STREAM_ERROR) {
        g_free(obuf);
        deflateEnd(&strm);
        return 1;
    }
    *outsize = bound - strm.avail_out;
    if (strm.avail_in != 0) {
        g_free(obuf);
        deflateEnd(&strm);
        return 1;
    }
    *out = g_malloc(*outsize);
    memcpy(*out, obuf, *outsize);
    g_free(obuf);
    deflateEnd(&strm);

    return 0;
}

int _page_cache_uncompress_buffer(unsigned char *in, gsize insize, unsigned char **out, gsize outsize)
{
    int ret;
    z_stream strm;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;

    ret = inflateInit(&strm);
    if (ret != Z_OK) {
        return 1;
    }
    *out = g_malloc(outsize);

    strm.avail_in = insize;
    strm.next_in = in;
    strm.avail_out = outsize;
    strm.next_out = *out;
    ret = inflate(&strm, Z_NO_FLUSH);
    if (ret == Z_STREAM_ERROR) {
        g_free(*out);
        inflateEnd(&strm);
        return 1;
    }
    if (strm.avail_in != 0) {
        g_free(*out);
        inflateEnd(&strm);
        return 1;
    }
    inflateEnd(&strm);

    return 0;
}
