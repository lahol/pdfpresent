#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdlib.h>
#include <memory.h>
#include "page-cache.h"
#include "page-overview.h"
#include "presentation.h"
#include "utils.h"
#include <time.h>

static gboolean _key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data);
static gboolean _configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer data);
static gboolean _draw_event(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean _delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
static gboolean _button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer data);
static gboolean _scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data);
static gboolean _motion_notify_event(GtkWidget *widget, GdkEventMotion *event, gpointer data);
static gboolean _window_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer data);
static void _window_realize(GtkWidget *widget, gpointer data);

void render_window(unsigned int id, cairo_t *cr);
static void render_presentation_window(cairo_t *cr, int width, int height);
static void render_console_window(cairo_t *cr, int width, int height);
static void render_overview_window(cairo_t *cr, int width, int height);
void main_recalc_window_page_display(void);
void main_reconfigure_windows(void);
int main_window_overview_get_grid_position(unsigned int id, int wx, int wy, guint *row, guint *column);
int main_window_to_page(unsigned int id, int wx, int wy, double *px, double *py);
int main_page_to_window(unsigned int id, double px, double py, int *wx, int *wy);

void toggle_fullscreen(guint win_id);

static void page_action_callback(unsigned int action, void *data);

void main_read_config(int argc, char **argv);
void config_step_notes(void);
void config_step_preview(void);
void config_toggle_console(void);
void main_cleanup(void);
void main_quit(void);

gboolean main_regular_update(gpointer data);
gboolean main_check_mouse_motion(gpointer data);

void main_reload_document(void);

void main_file_monitor_start(void);
void main_file_monitor_cleanup(void);
void main_file_monitor_cb(GFileMonitor *monitor, GFile *first, GFile *second, GFileMonitorEvent event, gpointer data);

void main_init_modes(void);

double overview_cell_width = 256.0f;
double overview_cell_height = 192.0f;
void main_set_overview_page_width(unsigned int width);
void main_prerender_overview_grid(void);

GThread *overview_grid_prerender_thread = NULL;
GMutex overview_grid_lock;
gint overview_grid_surface_status = 0; /* 0: invalid, 1: valid, 2: in prerender */
gint cancel_running_threads = 0;

GList *history_list = NULL;

void main_history_mark_current(void);
void main_history_back(void);

enum WindowMode {
    WINDOW_MODE_PRESENTATION = 0,
    WINDOW_MODE_CONSOLE,
    WINDOW_MODE_OVERVIEW
};

struct _Win {
    GtkWidget *win;
    gint cx;
    gint cy;
    void (*render)(cairo_t *, int, int);
    enum WindowMode window_mode;
    struct {
        UtilRect bounds;
        double scale;
    } page_display;
    unsigned int fullscreen : 1;
} windows[2];

struct _PresenterConfig {
    gchar *filename;
    unsigned int scale_to_height;
    unsigned int overview_page_width;
    unsigned int force_notes : 2; /* override guess, 1: override, show, 2: override, don't show, 0: use guess */
    unsigned int show_console : 1;
    unsigned int show_preview : 1;
    unsigned int disable_cache : 1;
    guint overview_columns;
    guint overview_rows;
} _config;

struct _PresenterState {
    double page_width;
    double page_height;
    int page_guess_split;
    GFile *document;
    GFileMonitor *monitor;
} _state;

struct _PresentationMode {
    void (*handle_reconfigure)(void);
    gboolean (*handle_key_press)(GtkWidget *, GdkEventKey *, gpointer);
    gboolean (*handle_button_press)(GtkWidget *, GdkEventButton *, gpointer);
    gboolean (*handle_scroll_event)(GtkWidget *, GdkEventScroll *, gpointer);
    gboolean (*handle_motion_event)(GtkWidget *, GdkEventMotion *, gpointer);
};

enum _PresentationModeType {
    PRESENTATION_MODE_NORMAL = 0,
    PRESENTATION_MODE_OVERVIEW,
    N_PRESENTATION_MODES
};

struct _PresentationMode mode_class[N_PRESENTATION_MODES];
enum _PresentationModeType current_mode;

void main_set_mode(enum _PresentationModeType mode);

GdkCursor *hand_cursor = NULL;
GdkCursor *blank_cursor = NULL;
guint hide_cursor_source = 0;
GTimer *hide_cursor_timer = NULL;

cairo_surface_t *overview_grid_surface = NULL;

int main(int argc, char **argv)
{
    unsigned int i;
    unsigned int w, h;

    gtk_init(&argc, &argv);

    g_mutex_init(&overview_grid_lock);

    main_init_modes();
    main_read_config(argc, argv);

    if (page_cache_init() != 0) {
        fprintf(stderr, "Failed to initialize page cache\n");
        return 1;
    }

    if (page_cache_load_document(_config.filename) != 0) {
        fprintf(stderr, "Error loading document\n");
        return 1;
    }

    page_overview_init(_config.overview_columns);
    page_overview_update();

    main_file_monitor_start();

    page_cache_set_scale_to_height(_config.scale_to_height);

    if (_config.disable_cache == 0)
        page_cache_start_caching();

    presentation_init(page_action_callback, NULL);

    main_set_overview_page_width(_config.overview_page_width);
    main_prerender_overview_grid();

    i = presentation_get_current_page();
    page_cache_fetch_page(i, NULL, &w, &h, &_state.page_guess_split);
    _state.page_width = (double)w;
    _state.page_height = (double)h;

    for (i = 0; i < 2; i++) {
        windows[i].win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        if (!windows[i].win) {
            fprintf(stderr, "Error creating window %d\n", i);
            main_cleanup();
            return 1;
        }
        g_signal_connect(windows[i].win,
                         "draw",
                         G_CALLBACK(_draw_event),
                         GUINT_TO_POINTER(i));
        g_signal_connect(windows[i].win,
                         "configure-event",
                         G_CALLBACK(_configure_event),
                         GUINT_TO_POINTER(i));
        g_signal_connect(windows[i].win,
                         "key-press-event",
                         G_CALLBACK(_key_press_event),
                         GUINT_TO_POINTER(i));
        g_signal_connect(windows[i].win,
                         "button-press-event",
                         G_CALLBACK(_button_press_event),
                         GUINT_TO_POINTER(i));
        g_signal_connect(windows[i].win,
                         "scroll-event",
                         G_CALLBACK(_scroll_event),
                         GUINT_TO_POINTER(i));
        g_signal_connect(windows[i].win,
                         "motion-notify-event",
                         G_CALLBACK(_motion_notify_event),
                         GUINT_TO_POINTER(i));
        g_signal_connect(windows[i].win,
                         "realize",
                         G_CALLBACK(_window_realize),
                         GUINT_TO_POINTER(i));
        g_signal_connect(windows[i].win,
                         "delete-event",
                         G_CALLBACK(_delete_event),
                         GUINT_TO_POINTER(i));
        g_signal_connect(windows[i].win,
                         "window-state-event",
                         G_CALLBACK(_window_state_event),
                         GUINT_TO_POINTER(i));
        gtk_widget_set_app_paintable(windows[i].win, TRUE);
        gtk_window_set_default_size(GTK_WINDOW(windows[i].win), 800, 600);
    }
    gtk_window_set_role(GTK_WINDOW(windows[0].win), "presentation");
    gtk_window_set_role(GTK_WINDOW(windows[1].win), "console");

    main_reconfigure_windows();

    gtk_widget_show_all(windows[0].win);
    gtk_widget_show_all(windows[1].win);

    hand_cursor = /*gdk_cursor_new(GDK_HAND2);*/
        gdk_cursor_new_from_name(gdk_display_get_default(), "pointer");

    g_timeout_add_seconds(1, (GSourceFunc)main_regular_update, NULL);
    hide_cursor_timer = g_timer_new();
    g_timer_start(hide_cursor_timer);
    hide_cursor_source = g_idle_add(main_check_mouse_motion, NULL);

    gtk_main();

    main_cleanup();

    return 0;
}

void main_cleanup(void)
{
    int i;
    main_file_monitor_cleanup();

    page_overview_cleanup();

    page_cache_stop_caching();
    page_cache_cleanup();
    for (i = 0; i < 2; i++) {
        if (GTK_IS_WINDOW(windows[i].win)) {
            gtk_widget_destroy(windows[i].win);
        }
    }
    g_free(_config.filename);

    if (overview_grid_surface)
        cairo_surface_destroy(overview_grid_surface);

    if (hide_cursor_timer)
        g_timer_destroy(hide_cursor_timer);
    if (hide_cursor_source)
        g_source_remove(hide_cursor_source);
    if (hand_cursor)
        g_object_unref(G_OBJECT(hand_cursor));
    if (blank_cursor)
        g_object_unref(G_OBJECT(blank_cursor));
}

static gboolean _key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    return mode_class[current_mode].handle_key_press(widget, event, data);
}

static gboolean _configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
    unsigned int id = GPOINTER_TO_UINT(data);
    if (id != 0 && id != 1) {
        return FALSE;
    }
    windows[id].cx = event->width;
    windows[id].cy = event->height;

    /* recalc overview rows */
    /* FIXME: currently only assume ration 4:3 */
    guint hr0 = 0, hr1 = 0;
    if (windows[0].cy != 0 && windows[0].cx != 0)
        hr0 = (windows[0].cy * _config.overview_columns * 4) / (windows[0].cx * 3);
    if (windows[1].cy != 0 && windows[1].cx != 0)
        hr1 = (windows[1].cy * _config.overview_columns * 4) / (windows[1].cx * 3);

    if (hr0 < hr1)
        _config.overview_rows = hr0;
    else
        _config.overview_rows = hr1;
    if (_config.overview_rows == 0)
        _config.overview_rows = 1;
    page_overview_set_display_rows(_config.overview_rows);

    gtk_widget_queue_draw(widget);

    return FALSE;
}

static gboolean _draw_event(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    render_window(GPOINTER_TO_UINT(data), cr);

    return FALSE;
}

static gboolean _delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    main_quit();
    return FALSE;
}

static gboolean _button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    return mode_class[current_mode].handle_button_press(widget, event, data);
}

static gboolean _scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    return mode_class[current_mode].handle_scroll_event(widget, event, data);
}

static gboolean _motion_notify_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    return mode_class[current_mode].handle_motion_event(widget, event, data);
}

static gboolean _window_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer data)
{
    unsigned int id = GPOINTER_TO_UINT(data);
    if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
        windows[id].fullscreen = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) ? 1 : 0;
    }
    return FALSE;
}

static void _window_realize(GtkWidget *widget, gpointer data)
{
    gdk_window_set_events(gtk_widget_get_window(widget),
                          gdk_window_get_events(gtk_widget_get_window(widget)) |
                          GDK_BUTTON_PRESS_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_SCROLL_MASK);
}

void render_window(unsigned int id, cairo_t *cr)
{
    if (id != 0 && id != 1) {
        return;
    }
    if (cr == NULL)
        return;

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_rectangle(cr, 0, 0, windows[id].cx, windows[id].cy);
    cairo_fill(cr);

    if (windows[id].render) {
        windows[id].render(cr, windows[id].cx, windows[id].cy);
    }
}

void main_render_page(cairo_t *cr, int index, int width, int height, int show_part, gboolean do_center)
{
    cairo_save(cr);

    cairo_surface_t *page_surface;
    unsigned int w, h;
    double scale, tmp;
    double ox = 0.0f, oy = 0.0f;
    double page_offset = 0.0f;
    int guess_split;

    if (page_cache_fetch_page(index, &page_surface, &w, &h, &guess_split) != 0) {
        fprintf(stderr, "could not fetch page %d\n", index);
        goto done;
    }

    if ((guess_split && _config.force_notes == 0) || _config.force_notes == 1) {
        w /= 2;
        if (!_config.show_preview && show_part == 1)
            page_offset = -((double)w);
    }

    scale = ((double)width)/((double)w);
    tmp = ((double)height)/((double)h);
    if (tmp < scale) scale = tmp;

    if (do_center) {
        ox = (width - scale * w) * 0.5f;
        oy = (height - scale * h) * 0.5f;
    }

    cairo_translate(cr, ox, oy);
    cairo_scale(cr, scale, scale);

    cairo_set_source_surface(cr, page_surface, page_offset, 0.0f);
    cairo_rectangle(cr, 0.0f, 0.0f, w, h);
    cairo_fill(cr);

done:

    cairo_restore(cr);
}

static void render_presentation_window(cairo_t *cr, int width, int height)
{
    main_render_page(cr, presentation_get_current_page(),
                     width, height, 0, TRUE);
}

static void render_console_window(cairo_t *cr, int width, int height)
{
    time_t tval;
    struct tm *curtval;
    char dbuf[256];
    char buffer[512];
    cairo_text_extents_t ext;
    PresentationStatus pstate;

    main_render_page(cr, presentation_get_current_page() + (_config.show_preview ? 1 : 0),
                         (int)(width * 0.8), (int)(height * 0.8), 1, FALSE);

    /* render time */
    time(&tval);
    curtval = localtime(&tval);
    strftime(dbuf, 256, "%H:%M:%S", curtval);

    presentation_get_status(&pstate);

    sprintf(buffer, "Cache: (%d/%d, %" G_GSIZE_FORMAT " bytes) %d/%d, %s",
            pstate.cached_pages, pstate.num_pages, pstate.cached_size,
            pstate.current_page, pstate.num_pages, dbuf);

    cairo_set_source_rgb(cr, 1.0f, 1.0f, 1.0f);
    cairo_set_font_size(cr, 24);
    cairo_text_extents(cr, buffer, &ext);
    cairo_move_to(cr, width-ext.x_advance, height+ext.y_bearing);
    cairo_show_text(cr, buffer);
}

void render_overview_window_page_thumbnail(cairo_t *cr, gint index, gchar *label, guint row, guint column)
{
    cairo_text_extents_t ext;

    cairo_save(cr);
    /* horizontal center in cell */
    cairo_translate(cr, (column + 0.05) * overview_cell_width, row * overview_cell_height);
    main_render_page(cr, index, overview_cell_width * 0.9f, overview_cell_height * 0.9f, 0, FALSE);

    cairo_set_source_rgb(cr, 1.0f, 1.0f, 1.0f);
    cairo_set_font_size(cr, 8);
    cairo_text_extents(cr, label, &ext);
    cairo_move_to(cr, overview_cell_width - ext.x_advance - 0.1f * overview_cell_width,
                      overview_cell_height + ext.y_bearing);
    cairo_show_text(cr, label);

    /* Draw outline */
    cairo_set_source_rgb(cr, 0.0f, 0.0f, 0.0f);
    cairo_set_line_width(cr, 0.2f);
    cairo_move_to(cr, overview_cell_width - ext.x_advance - 0.1f * overview_cell_width,
                      overview_cell_height + ext.y_bearing);
    cairo_text_path(cr, label);
    cairo_stroke(cr);

    cairo_restore(cr);
}

static void render_overview_window(cairo_t *cr, int width, int height)
{
    guint col, row;

    double scale = ((double)width)/(_config.overview_columns * overview_cell_width);
    cairo_scale(cr, scale, scale);

    if (g_atomic_int_get(&overview_grid_surface_status) == 1) {

        g_mutex_lock(&overview_grid_lock);
        cairo_set_source_surface(cr, overview_grid_surface, 0.0, -(overview_cell_height * page_overview_get_offset()));
        cairo_rectangle(cr, 0.0f, 0.0f, width / scale, height / scale);
        cairo_fill(cr);
        g_mutex_unlock(&overview_grid_lock);
    }
    else {
        gint index;
        gchar *label;

        for (col = 0; col < _config.overview_columns; ++col) {
            for (row = 0; row < _config.overview_rows; ++row) {
                if (page_overview_get_page(row, col, &index, &label, FALSE)) {
                    render_overview_window_page_thumbnail(cr, index, label, row, col);
                }
            }
        }
    }

    page_overview_get_selection(&row, &col);
    cairo_set_source_rgb(cr, 1.0f, 0.0f, 0.0f);
    cairo_rectangle(cr, col * overview_cell_width, row * overview_cell_height,
                        overview_cell_width, overview_cell_height);
    cairo_stroke(cr);
}

gpointer _main_prerender_overview_grid_thread_proc(gpointer null)
{
    guint rows, columns;
    guint c, r;
    gint index;
    gint success = 0;
    gchar *label;

    page_overview_get_grid_size(&rows, &columns);

    g_atomic_int_set(&overview_grid_surface_status, 2);

    g_mutex_lock(&overview_grid_lock);

    if (overview_grid_surface)
        cairo_surface_destroy(overview_grid_surface);

    overview_grid_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            (int)(overview_cell_width * columns),
            (int)(overview_cell_height * rows));

    if (cairo_surface_status(overview_grid_surface) != CAIRO_STATUS_SUCCESS)
        goto done;

    cairo_t *cr = cairo_create(overview_grid_surface);
    cairo_set_source_rgb(cr, 0.0f, 0.0f, 0.0f);
    cairo_rectangle(cr, 0, 0, overview_cell_width * columns, overview_cell_height * rows);
    cairo_fill(cr);

    for (r = 0; r < rows; ++r) {
        for (c = 0; c < columns; ++c) {
            if (g_atomic_int_get(&cancel_running_threads))
                goto cancel;
            if (page_overview_get_page(r, c, &index, &label, TRUE)) {
                render_overview_window_page_thumbnail(cr, index, label, r, c);
            }
        }
    }


    success = 1;

cancel:
    cairo_destroy(cr);

done:
    g_mutex_unlock(&overview_grid_lock);
    g_atomic_int_set(&overview_grid_surface_status, success);

    return NULL;
}

void main_set_overview_page_width(unsigned int width)
{
    /* FIXME: still assumes 4:3 ratio */
    /* FIXME: assumes 4 columns */
    overview_cell_width = 0.25f * width;
    overview_cell_height = 0.1875 * width;
}

void main_prerender_overview_grid(void)
{
    overview_grid_prerender_thread = g_thread_new("OverviewGrid", (GThreadFunc)_main_prerender_overview_grid_thread_proc, NULL);
    g_thread_unref(overview_grid_prerender_thread);
}

int main_window_overview_get_grid_position(unsigned int id, int wx, int wy, guint *row, guint *column)
{
    if (windows[id].window_mode != WINDOW_MODE_OVERVIEW)
        return -1;

    double scale = ((double)windows[id].cx)/(_config.overview_columns * overview_cell_width);
    guint c = (guint)((wx) / (overview_cell_width * scale));
    guint r = (guint)((wy) / (overview_cell_height * scale));

    if (row)
        *row = r;
    if (column)
        *column = c;
    return 0;
}

int main_window_to_page(unsigned int id, int wx, int wy, double *px, double *py)
{
    double scale, tmp;
    double ox, oy;
    double dw;
    double rx, ry;
    if ((_state.page_guess_split && _config.force_notes == 0) || _config.force_notes == 1) {
        dw = 0.5f * _state.page_width;
    }
    else {
        dw = _state.page_width;
    }
    if (id == 0 || (id == 1 && (!_config.show_console || _config.show_preview))) {
        scale = ((double)windows[id].cx)/dw;
        tmp = ((double)windows[id].cy)/_state.page_height;
        if (tmp < scale) scale = tmp;
        ox = (windows[id].cx-scale*dw)*0.5f;
        oy = (windows[id].cy-scale*_state.page_height)*0.5f;
        rx = (wx-ox)/scale;
        ry = _state.page_height-(wy-oy)/scale;
        if (px) *px = rx;
        if (py) *py = ry;
        if (rx >= 0.0f && ry >= 0.0f && rx <= dw && ry <= _state.page_height) {
            return 0;
        }
        else {
            return 1;
        }
    }
    else if (id == 1 && _config.show_console) {
        scale = ((double)windows[id].cx)/dw;
        tmp = ((double)windows[id].cy)/_state.page_height;
        if (tmp < scale) scale = tmp;
        scale *= 0.8f;
        if ((_state.page_guess_split && _config.force_notes == 0) || _config.force_notes == 1) {
            ox = dw;
        }
        else {
            ox = 0.0f;
        }

        rx = wx/scale+ox;
        ry = _state.page_height-wy/scale;
        if (px) *px = rx;
        if (py) *py = ry;
        if (rx >= ox && ry >= 0.0f && rx <= dw+ox && ry <= _state.page_height) {
            return 0;
        }
        else {
            return 1;
        }
    }
    else {
        return -1;
    }
}

int main_page_to_window(unsigned int id, double px, double py, int *wx, int *wy)
{
    return 0;
}

static void page_action_callback(unsigned int action, void *data)
{
    unsigned int index;
    unsigned int w, h;

    switch (action) {
        case PRESENTATION_ACTION_PAGE_CHANGED:
            index = presentation_get_current_page();
            page_cache_fetch_page(index, NULL, &w, &h, &_state.page_guess_split);
            _state.page_width = (double)w;
            _state.page_height = (double)h;
            gtk_widget_queue_draw(windows[0].win);
            gtk_widget_queue_draw(windows[1].win);
            break;
        case PRESENTATION_ACTION_QUIT:
            main_quit();
            break;
        default:
            fprintf(stderr, "Unhandled action id: %d\n", action);
    }
}

gboolean _main_parse_option(const gchar *option_name, const gchar *value, gpointer data, GError **error)
{
    int val;
    if (g_strcmp0(value, "yes") == 0 ||
            g_strcmp0(value, "on") == 0 ||
            g_strcmp0(value, "1") == 0)
        val = 1;
    else if (g_strcmp0(value, "off") == 0 ||
             g_strcmp0(value, "no") == 0 ||
             g_strcmp0(value, "0") == 0)
        val = 0;
    else if (g_strcmp0(value, "guess") == 0)
        val = 2;
    else if (value) {
        return FALSE;
    }
    if (g_strcmp0(option_name, "--console") == 0 ||
            g_strcmp0(option_name, "-c") == 0) {
        if (value)
            _config.show_console = val;
        else
            _config.show_console = 1;
    }
    else if (g_strcmp0(option_name, "--notes") == 0 ||
             g_strcmp0(option_name, "-n") == 0) {
        if (value)
            _config.force_notes = 2-val;
        else
            _config.force_notes = 0;
    }
    else if (g_strcmp0(option_name, "--no-cache") == 0) {
        _config.disable_cache = 1;
    }
    else if (g_strcmp0(option_name, "--preview") == 0 ||
             g_strcmp0(option_name, "-p") == 0) {
        if (value)
            _config.show_preview = val;
        else
            _config.show_preview = 1;
    }
    else {
        return FALSE;
    }
    return TRUE;
}

static GOptionEntry entries[] = {
    { "console", 'c', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, _main_parse_option, "Show console at startup", "value" },
    { "notes", 'n', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, _main_parse_option, "Assume there are notes or not, guess value", "value" },
    { "preview", 'p', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, _main_parse_option, "Show preview of next slide", "value" },
    { "height", 'h', 0, G_OPTION_ARG_INT, &_config.scale_to_height, "Use pixmap of this height for prerendering", "N" },
    { "no-cache", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, _main_parse_option, "Do not cache pages", NULL },
    { "overview-page-width", 'w', 0, G_OPTION_ARG_INT, &_config.overview_page_width, "Prerender overview page to this width", "N" },
    { NULL }
};

void main_read_config(int argc, char **argv)
{
    /* set defaults */
    memset(&_config, 0, sizeof(struct _PresenterConfig));
    _config.show_console = 1;
    _config.force_notes = 0;
    _config.scale_to_height = 768;
    _config.overview_columns = 4;
    _config.overview_rows = 3;
    _config.overview_page_width = 1024;

    GError *error = NULL;;
    GOptionContext *context = g_option_context_new("FILE - make two window presentations with presenter console");

    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));
    if (!g_option_context_parse(context, &argc, &argv, NULL)) {
        fprintf(stderr, "option parsing failed: %s\n", error ? error->message : NULL);
        g_option_context_free(context);
        if (error)
            g_error_free(error);
        exit(1);
    }

    g_option_context_free(context);

    if (argc <= 1) {
        fprintf(stderr, "no filename given\n");
        exit(1);
    }

    _config.filename = g_strdup(argv[1]);

}

void main_quit(void)
{
    /* if we are still in prerender thread, cancel it */
    if (g_atomic_int_get(&overview_grid_surface_status) == 2) {
        g_atomic_int_set(&cancel_running_threads, 1);
        g_mutex_lock(&overview_grid_lock);
        g_mutex_unlock(&overview_grid_lock);
    }
    gtk_main_quit();
}

void main_reconfigure_windows(void)
{
    mode_class[current_mode].handle_reconfigure();
}

void main_recalc_window_page_display(void)
{
    /* page_display, bounds, scale */
}

void config_step_notes(void)
{
    if (_config.force_notes == 2) {
        _config.force_notes = 0;
    }
    else {
        _config.force_notes++;
    }
    main_reconfigure_windows();
}

void config_step_preview(void)
{
    _config.show_preview = !_config.show_preview;
    main_reconfigure_windows();
}

void config_toggle_console(void)
{
    _config.show_console = !_config.show_console;
    main_reconfigure_windows();
}

gboolean main_regular_update(gpointer data)
{
    if (_config.show_console) {
        gtk_widget_queue_draw(windows[1].win);
    }
    return TRUE;
}

void main_update(void)
{
    gtk_widget_queue_draw(windows[0].win);
    gtk_widget_queue_draw(windows[1].win);
}

gboolean main_check_mouse_motion(gpointer data)
{
    if (!blank_cursor)
        blank_cursor = /*gdk_cursor_new(GDK_BLANK_CURSOR);*/
            gdk_cursor_new_from_name(gdk_display_get_default(), "none");
    gdouble elapsed = g_timer_elapsed(hide_cursor_timer, NULL);
    if (elapsed > 3) {
        g_timer_stop(hide_cursor_timer);
        g_timer_reset(hide_cursor_timer);
        hide_cursor_source = 0;
        gdk_window_set_cursor(gtk_widget_get_window(windows[0].win), blank_cursor);
        gdk_window_set_cursor(gtk_widget_get_window(windows[1].win), blank_cursor);
        return FALSE;
    }
    return TRUE;
}

void main_reload_document(void)
{
    page_cache_stop_caching();
    page_cache_unload_document();

    page_cache_load_document(_config.filename);
    if (_config.disable_cache == 0)
        page_cache_start_caching();

    presentation_update();

    page_overview_update();
    main_prerender_overview_grid();
}

void toggle_fullscreen(guint win_id)
{
    if (win_id >= 2) {
        return;
    }

    if (windows[win_id].fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(windows[win_id].win));
    }
    else {
        gtk_window_fullscreen(GTK_WINDOW(windows[win_id].win));
    }
}

void main_file_monitor_start(void)
{
    gchar *uri = util_make_uri(_config.filename);
    if (uri == NULL)
        return;
    _state.document = g_file_new_for_uri(uri);
    g_free(uri);

    _state.monitor = g_file_monitor(_state.document, G_FILE_MONITOR_SEND_MOVED, NULL, NULL);
    if (_state.monitor == NULL)
        return;

    g_signal_connect(_state.monitor, "changed", G_CALLBACK(main_file_monitor_cb), NULL);
}

void main_file_monitor_cleanup(void)
{
    if (_state.monitor)
        g_object_unref(_state.monitor);
    if (_state.document)
        g_object_unref(_state.document);

    _state.monitor = NULL;
    _state.document = NULL;
}

void main_file_monitor_cb(GFileMonitor *monitor, GFile *first, GFile *second, GFileMonitorEvent event, gpointer data)
{
    /* TODO: Handle move; change: start timer to reload if changes_done_hint isn’t there yet */
    if (event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
        main_reload_document();
    }
}

void mode_normal_reconfigure_windows(void)
{
    windows[0].render = render_presentation_window;
    windows[0].window_mode = WINDOW_MODE_PRESENTATION;
    if (_config.show_console) {
        windows[1].render = render_console_window;
        windows[1].window_mode = WINDOW_MODE_CONSOLE;
    }
    else {
        windows[1].render = render_presentation_window;
        windows[1].window_mode = WINDOW_MODE_OVERVIEW;
    }
}

gboolean mode_normal_handle_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    int do_reconfigure = 0;
    switch (event->keyval) {
        case GDK_KEY_Left:
        case GDK_KEY_Up:
        case GDK_KEY_Prior:
            presentation_page_prev();
            break;
        case GDK_KEY_Right:
        case GDK_KEY_Down:
        case GDK_KEY_KP_Space:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_Return:
        case GDK_KEY_space:
        case GDK_KEY_Next:
            presentation_page_next();
            break;
        case GDK_KEY_Home:
            main_history_mark_current();
            presentation_page_first();
            break;
        case GDK_KEY_End:
            main_history_mark_current();
            presentation_page_last();
            break;
        case GDK_KEY_n:
            config_step_notes();
            do_reconfigure = 1;
            break;
        case GDK_KEY_c:
            config_toggle_console();
            do_reconfigure = 1;
            break;
        case GDK_KEY_f:
            toggle_fullscreen(GPOINTER_TO_UINT(data));
            do_reconfigure = 1;
            break;
        case GDK_KEY_r:
            main_reload_document();
            break;
        case GDK_KEY_p:
            config_step_preview();
            break;
/*        case GDK_KEY_Escape:*/
        case GDK_KEY_o:
            if (event->state & GDK_CONTROL_MASK) {
                main_history_back();
            }
            break;
        case GDK_KEY_q:
            main_quit();
            break;
        case GDK_KEY_Tab:
            page_overview_set_page(presentation_get_current_page());
            main_set_mode(PRESENTATION_MODE_OVERVIEW);
            break;
    }

    if (do_reconfigure) {
        gtk_widget_queue_draw(windows[0].win);
        gtk_widget_queue_draw(windows[1].win);
    }

    return FALSE;
}

gboolean mode_normal_handle_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    unsigned int id = GPOINTER_TO_UINT(data);
    double px, py;
    int found_link = 0;
    /* TODO: Handle preview pages correctly */
    if (!_config.show_preview && event->button == 1 && main_window_to_page(id, (int)event->x, (int)event->y, &px, &py) == 0) {
        if (presentation_perform_action_at(px, py) == 0) {
            found_link = 1;
        }
    }
    if (!found_link) {
        if (event->button == 1 && event->type == GDK_BUTTON_PRESS) {
            presentation_page_next();
        }
        else if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {
            presentation_page_prev();
        }
    }
    return TRUE;
}

gboolean mode_normal_handle_scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    if (event->direction == GDK_SCROLL_UP)
        presentation_page_prev();
    else if (event->direction == GDK_SCROLL_DOWN)
        presentation_page_next();
    return FALSE;
}

gboolean mode_normal_handle_motion_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    unsigned int id = GPOINTER_TO_UINT(data);
    int found_link = 0;
    double px, py;
    if (!_config.show_preview && main_window_to_page(id, (int)event->x, (int)event->y, &px, &py) == 0) {
        if (page_cache_get_action_from_pos(px, py)) {
            found_link = 1;
        }
    }
    if (found_link) {
        gdk_window_set_cursor(gtk_widget_get_window(widget), hand_cursor);
    }
    else {
        gdk_window_set_cursor(gtk_widget_get_window(widget), NULL);
    }
    if (!hide_cursor_timer)
        hide_cursor_timer = g_timer_new();
    g_timer_start(hide_cursor_timer);
    if (!hide_cursor_source)
        hide_cursor_source = g_idle_add(main_check_mouse_motion, NULL);
    return FALSE;
}

void mode_overview_reconfigure_windows(void)
{
    windows[1].render = render_overview_window;
    windows[1].window_mode = WINDOW_MODE_OVERVIEW;

    if (_config.show_console) {
        windows[0].render = render_presentation_window;
        windows[0].window_mode = WINDOW_MODE_PRESENTATION;
    }
    else {
        windows[0].render = render_overview_window;
        windows[0].window_mode = WINDOW_MODE_OVERVIEW;
    }
}

gboolean mode_overview_handle_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    switch (event->keyval) {
        case GDK_KEY_Tab:
        case GDK_KEY_Escape:
            main_set_mode(PRESENTATION_MODE_NORMAL);
            break;
        case GDK_KEY_Left:
            page_overview_move(-1, 0);
            break;
        case GDK_KEY_Right:
            page_overview_move(1, 0);
            break;
        case GDK_KEY_Down:
            page_overview_move(0, 1);
            break;
        case GDK_KEY_Up:
            page_overview_move(0, -1);
            break;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            {
                main_history_mark_current();
                gint page_index = page_overview_get_selection(NULL, NULL);
                presentation_page_goto(page_index);

                main_set_mode(PRESENTATION_MODE_NORMAL);
            }
            break;
        case GDK_KEY_Home:
            break;
        case GDK_KEY_End:
            break;
        case GDK_KEY_f:
            toggle_fullscreen(GPOINTER_TO_UINT(data));
            break;
        case GDK_KEY_r:
            main_reload_document();
            break;
        case GDK_KEY_q:
            main_quit();
            break;
        default:
            break;
    }

    gtk_widget_queue_draw(windows[0].win);
    gtk_widget_queue_draw(windows[1].win);

    return FALSE;
}

gboolean mode_overview_handle_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    unsigned int id = GPOINTER_TO_UINT(data);

    guint row, column;

    if (event->button == 1) {
        if (main_window_overview_get_grid_position(id, (int)event->x, (int)event->y, &row, &column) != 0)
            return FALSE;

        gint index;
        if (page_overview_get_page(row, column, &index, NULL, FALSE)) {
            presentation_page_goto(index);
            main_set_mode(PRESENTATION_MODE_NORMAL);
        }
        return TRUE;
    }

    return FALSE;
}

gboolean mode_overview_handle_scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    if (event->direction == GDK_SCROLL_UP)
        page_overview_scroll(-1);
    else if (event->direction == GDK_SCROLL_DOWN)
        page_overview_scroll(1);

    gtk_widget_queue_draw(windows[0].win);
    gtk_widget_queue_draw(windows[1].win);

    return TRUE;
}

gboolean mode_overview_handle_motion_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    gdk_window_set_cursor(gtk_widget_get_window(widget), NULL);
    if (!hide_cursor_timer)
        hide_cursor_timer = g_timer_new();
    g_timer_start(hide_cursor_timer);
    if (!hide_cursor_source)
        hide_cursor_source = g_idle_add(main_check_mouse_motion, NULL);
    return FALSE;
}

void main_set_mode(enum _PresentationModeType mode)
{
    if (current_mode == mode)
        return;
    current_mode = mode;
    main_reconfigure_windows();

    gtk_widget_queue_draw(windows[0].win);
    gtk_widget_queue_draw(windows[1].win);
}

void main_init_modes(void)
{
    mode_class[PRESENTATION_MODE_NORMAL].handle_reconfigure =
        mode_normal_reconfigure_windows;
    mode_class[PRESENTATION_MODE_NORMAL].handle_key_press =
        mode_normal_handle_key_press;
    mode_class[PRESENTATION_MODE_NORMAL].handle_button_press =
        mode_normal_handle_button_press;
    mode_class[PRESENTATION_MODE_NORMAL].handle_scroll_event =
        mode_normal_handle_scroll_event;
    mode_class[PRESENTATION_MODE_NORMAL].handle_motion_event =
        mode_normal_handle_motion_event;

    mode_class[PRESENTATION_MODE_OVERVIEW].handle_reconfigure =
        mode_overview_reconfigure_windows;
    mode_class[PRESENTATION_MODE_OVERVIEW].handle_key_press =
        mode_overview_handle_key_press;
    mode_class[PRESENTATION_MODE_OVERVIEW].handle_button_press =
        mode_overview_handle_button_press;
    mode_class[PRESENTATION_MODE_OVERVIEW].handle_scroll_event =
        mode_overview_handle_scroll_event;
    mode_class[PRESENTATION_MODE_OVERVIEW].handle_motion_event =
        mode_overview_handle_motion_event;
}

void main_history_mark_current(void)
{
    gint index = presentation_get_current_page();

    history_list = g_list_prepend(history_list, GINT_TO_POINTER(index));
}

void main_history_back(void)
{
    if (!history_list)
        return;
    gint index = GPOINTER_TO_INT(history_list->data);

    history_list = g_list_delete_link(history_list, history_list);

    presentation_page_goto(index);
}
