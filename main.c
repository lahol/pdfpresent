#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdlib.h>
#include <memory.h>
#include "page-cache.h"
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
    unsigned int force_notes : 2; /* override guess, 1: override, show, 2: override, don't show, 0: use guess */
    unsigned int show_console : 1;
    unsigned int show_preview : 1;
    unsigned int disable_cache : 1;
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

int main(int argc, char **argv)
{
    unsigned int i;
    unsigned int w, h;

    gtk_init(&argc, &argv);

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

    main_file_monitor_start();

    page_cache_set_scale_to_height(_config.scale_to_height);

    if (_config.disable_cache == 0)
        page_cache_start_caching();

    presentation_init(page_action_callback, NULL);
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
    gtk_window_set_wmclass(GTK_WINDOW(windows[0].win), "presentation", "Pdfpresent");
    gtk_window_set_wmclass(GTK_WINDOW(windows[1].win), "console", "Pdfpresent");

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

    page_cache_stop_caching();
    page_cache_cleanup();
    for (i = 0; i < 2; i++) {
        if (GTK_IS_WINDOW(windows[i].win)) {
            gtk_widget_destroy(windows[i].win);
        }
    }
    g_free(_config.filename);
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
    if (event->direction == GDK_SCROLL_UP)
        presentation_page_prev();
    else if (event->direction == GDK_SCROLL_DOWN)
        presentation_page_next();
    return FALSE;
}

static gboolean _motion_notify_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
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

static void render_presentation_window(cairo_t *cr, int width, int height)
{
    cairo_surface_t *page_surface;
    unsigned int w, h;
    unsigned int index;
    double scale, tmp;
    double ox, oy;
    int guess_split;

    index = presentation_get_current_page();
    if (page_cache_fetch_page(index, &page_surface, &w, &h, &guess_split) != 0) {
        fprintf(stderr, "could not fetch page %d\n", index);
        return;
    }
    if ((guess_split && _config.force_notes == 0) || _config.force_notes == 1) {
        w /= 2;
    }
    scale = ((double)width)/((double)w);
    tmp = ((double)height)/((double)h);
    if (tmp < scale) scale = tmp;

    ox = (width-scale*w)*0.5f;
    oy = (height-scale*h)*0.5f;

    cairo_translate(cr, ox, oy);
    cairo_scale(cr, scale, scale);

    cairo_set_source_surface(cr, page_surface, 0.0f, 0.0f);
    cairo_rectangle(cr, 0.0f, 0.0f, w, h);
    cairo_fill(cr);
}

static void render_console_window(cairo_t *cr, int width, int height)
{
    cairo_surface_t *page_surface;
    unsigned int w, h;
    unsigned int index;
    double scale, tmp;
    double page_offset = 0.0f;
    int guess_split;
    cairo_matrix_t m;
    time_t tval;
    struct tm *curtval;
    char dbuf[256];
    char buffer[512];
    cairo_text_extents_t ext;
    PresentationStatus pstate;

    index = presentation_get_current_page();
    if (page_cache_fetch_page(index + (_config.show_preview ? 1 : 0), &page_surface, &w, &h, &guess_split) == 0) {
        if ((guess_split && _config.force_notes == 0) || _config.force_notes == 1) {
            w /= 2;
            if (!_config.show_preview)
                page_offset = -((double)w);
        }
        scale = ((double)width)/((double)w);
        tmp = ((double)height)/((double)h);
        if (tmp < scale) scale = tmp;
        scale *= 0.8f;

        cairo_get_matrix(cr, &m);
        cairo_scale(cr, scale, scale);

        cairo_set_source_surface(cr, page_surface, page_offset, 0.0f);
        cairo_rectangle(cr, 0.0f, 0.0f, w, h);
        cairo_fill(cr);

        cairo_set_matrix(cr, &m);
    }

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

static void render_overview_window(cairo_t *cr, int width, int height)
{
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
    { "no-cache", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, _main_parse_option, "Do not cache pages", NULL }
};

void main_read_config(int argc, char **argv)
{
    /* set defaults */
    memset(&_config, 0, sizeof(struct _PresenterConfig));
    _config.show_console = 1;
    _config.force_notes = 0;
    _config.scale_to_height = 768;

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
            presentation_page_first();
            break;
        case GDK_KEY_End:
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
        case GDK_KEY_q:
            main_quit();
            break;
        case GDK_KEY_Tab:
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
        if (event->button == 1) {
            presentation_page_next();
        }
        else if (event->button == 3) {
            presentation_page_prev();
        }
    }
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
            main_set_mode(PRESENTATION_MODE_NORMAL);
            break;
        case GDK_KEY_Left:
            break;
        case GDK_KEY_Right:
            break;
        case GDK_KEY_Down:
            break;
        case GDK_KEY_Up:
            break;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
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

    mode_class[PRESENTATION_MODE_OVERVIEW].handle_reconfigure =
        mode_overview_reconfigure_windows;
    mode_class[PRESENTATION_MODE_OVERVIEW].handle_key_press =
        mode_overview_handle_key_press;
    mode_class[PRESENTATION_MODE_OVERVIEW].handle_button_press =
        mode_overview_handle_button_press;
}
