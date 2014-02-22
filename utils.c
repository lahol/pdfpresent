#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <glib.h>

#ifdef OWN_G_LIST_FREE_FULL
void g_list_free_full(GList *list, GDestroyNotify free_func)
{
    GList *tmp;
    if (free_func) {
        tmp = list;
        while (tmp) {
            free_func(tmp->data);
            tmp = tmp->next;
        }
    }
    g_list_free(list);
}
#endif

gchar *util_make_uri(const gchar *file)
{
    gchar *buf;
    gchar *scheme;
    gchar *cur_dir = NULL;
    gchar *current;
    gsize path_len = 0;

    if (!file) return NULL;

    scheme = g_uri_parse_scheme(file);
    if (!scheme) {
        path_len += strlen("file://");
        if (!g_path_is_absolute(file)) {
            cur_dir = g_get_current_dir();
            path_len += strlen(cur_dir);
            if (!G_IS_DIR_SEPARATOR(cur_dir[strlen(cur_dir)-1])) {
                path_len++;
            }
        }
        path_len += strlen(file);
        buf = g_malloc(sizeof(gchar)*(path_len+1));
        current = g_stpcpy(buf, "file://");
        if (cur_dir) {
            current = g_stpcpy(current, cur_dir);
            if (!G_IS_DIR_SEPARATOR(cur_dir[strlen(cur_dir)-1])) {
                current = g_stpcpy(current, G_DIR_SEPARATOR_S);
            }
        }
        /*current =*/ g_stpcpy(current, file);
        return buf;
    }
    else {
        return g_strdup(file);
    }
}

int util_rects_overlap(UtilRect *a, UtilRect *b)
{
    if (!a || !b) return 0;
    if (a->x2 >= b->x1 && a->x1 <= b->x2 &&
            a->y1 >= b->y2 && a->y2 <= b->y1) {
        return 1;
    }
    return 0;
}

int util_point_in_rect(UtilPoint *pt, UtilRect *rect)
{
    if (pt && rect && ((pt->x >= rect->x1 && pt->x <= rect->x2) ||
                       (pt->x <= rect->x1 && pt->x >= rect->x2)) &&
            ((pt->y >= rect->y1 && pt->y <= rect->y2) ||
             (pt->y <= rect->y1 && pt->y >= rect->y2))) {
        return 1;
    }
    else {
        return 0;
    }
}


