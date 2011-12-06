#ifndef __UTILS_H__
#define __UTILS_H__

#include <glib.h>

#ifndef g_list_free_full
void g_list_free_full(GList *list, GDestroyNotify free_func);
#define OWN_G_LIST_FREE_FULL
#endif

typedef struct _UtilRect {
  double x1;
  double y1;
  double x2;
  double y2;
} UtilRect;

typedef struct _UtilPoint {
  double x;
  double y;
} UtilPoint;

gchar *util_make_uri(const gchar *file);
int util_rects_overlap(UtilRect *a, UtilRect *b);
int util_point_in_rect(UtilPoint *pt, UtilRect *rect);

#endif
