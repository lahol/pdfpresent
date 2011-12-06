#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdlib.h>
#include <memory.h>
#include "page-cache.h"
#include "presentation.h"
#include "utils.h"

static gboolean _key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data);
static gboolean _configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer data);
static gboolean _expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer data);
static gboolean _delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
static gboolean _button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer data);
static gboolean _motion_notify_event(GtkWidget *widget, GdkEventMotion *event, gpointer data);
static void _window_realize(GtkWidget *widget, gpointer data);

void render_window(unsigned int id);
static void render_presentation_window(cairo_t *cr, int width, int height);
static void render_console_window(cairo_t *cr, int width, int height);
void main_recalc_window_page_display(void);
void main_reconfigure_windows(void);
int main_window_to_page(unsigned int id, int wx, int wy, double *px, double *py);
int main_page_to_window(unsigned int id, double px, double py, int *wx, int *wy);

static void page_action_callback(unsigned int action, void *data);

void main_read_config(int argc, char **argv);
void config_step_notes(void);
void config_toggle_console(void);
void main_cleanup(void);
void main_quit(void);

struct _Win {
  GtkWidget *win;
  gint cx;
  gint cy;
  void (*render)(cairo_t*, int, int);
  struct {
    UtilRect bounds;
    double scale;
  } page_display;
} windows[2];

struct _PresenterConfig {
  gchar *filename;
  unsigned int scale_to_height;
  unsigned int force_notes : 2; /* override guess, 1: override, show, 2: override, don't show, 0: use guess */
  unsigned int show_console : 1;
  unsigned int show_preview : 1;
} _config;

struct _PresenterState {
  double page_width;
  double page_height;
  int page_guess_split;
} _state;

int main(int argc, char **argv) {
  unsigned int i;
  unsigned int w, h;

  gtk_init(&argc, &argv);
  g_thread_init(NULL);

  main_read_config(argc, argv);

  if (page_cache_load_document(_config.filename, _config.scale_to_height) != 0) {
    fprintf(stderr, "Error loading document\n");
    return 1;
  }
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
                     "expose-event",
                     G_CALLBACK(_expose_event),
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
    gtk_widget_set_app_paintable(windows[i].win, TRUE);
    gtk_window_set_default_size(GTK_WINDOW(windows[i].win), 800, 600);
  }
  gtk_window_set_wmclass(GTK_WINDOW(windows[0].win), "presentation", "Pdfpresent");
  gtk_window_set_wmclass(GTK_WINDOW(windows[1].win), "console", "Pdfpresent");

  main_reconfigure_windows();

  gtk_widget_show_all(windows[0].win);
  gtk_widget_show_all(windows[1].win);

  gtk_main();

  main_cleanup();

  return 0;
}

void main_cleanup(void) {
  int i;
  page_cache_unload_document();
  for (i = 0; i < 2; i++) {
    if (GTK_IS_WINDOW(windows[i].win)) {
      gtk_widget_destroy(windows[i].win);
    }
  }
  g_free(_config.filename);
}

/* be downwards comaptible */
#ifndef GDK_KEY_Left
#define GDK_KEY_Left GDK_Left
#endif
#ifndef GDK_KEY_Right
#define GDK_KEY_Right GDK_Right
#endif
#ifndef GDK_KEY_Down
#define GDK_KEY_Down GDK_Down
#endif
#ifndef GDK_KEY_Up
#define GDK_KEY_Up GDK_Up
#endif
#ifndef GDK_KEY_KP_Space
#define GDK_KEY_KP_Space GDK_KP_Space
#endif
#ifndef GDK_KEY_KP_Enter
#define GDK_KEY_KP_Enter GDK_KP_Enter
#endif
#ifndef GDK_KEY_p
#define GDK_KEY_p GDK_p
#endif
#ifndef GDK_KEY_n
#define GDK_KEY_n GDK_n
#endif
#ifndef GDK_KEY_c
#define GDK_KEY_c GDK_c
#endif
#ifndef GDK_KEY_Escape
#define GDK_KEY_Escape GDK_Escape
#endif
#ifndef GDK_KEY_d
#define GDK_KEY_d GDK_d
#endif
#ifndef GDK_KEY_Home
#define GDK_KEY_Home GDK_Home
#endif
#ifndef GDK_KEY_End
#define GDK_KEY_End GDK_End
#endif

static gboolean _key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data) {
  int do_reconfigure = 0;
  switch (event->keyval) {
    case GDK_KEY_Left:
    case GDK_KEY_Up:
      presentation_page_prev();
      break;
    case GDK_KEY_Right:
    case GDK_KEY_Down:
    case GDK_KEY_KP_Space:
    case GDK_KEY_KP_Enter:
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
    case GDK_KEY_Escape:
      main_quit();
      break;
  }

  if (do_reconfigure) {
    gtk_widget_queue_draw(windows[0].win);
    gtk_widget_queue_draw(windows[1].win);
  }

  return FALSE;
}

static gboolean _configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
  unsigned int id = GPOINTER_TO_UINT(data);
  if (id != 0 && id != 1) {
    return FALSE;
  }
  windows[id].cx = event->width;
  windows[id].cy = event->height;

  gtk_widget_queue_draw(widget);

  return FALSE;
}

static gboolean _expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
  render_window(GPOINTER_TO_UINT(data));

  return FALSE;
}

static gboolean _delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
  main_quit();
  return FALSE;
}

static gboolean _button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  unsigned int id = GPOINTER_TO_UINT(data);
  double px, py;
  int found_link = 0;
  if (main_window_to_page(id, (int)event->x, (int)event->y, &px, &py) == 0) {
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

static gboolean _motion_notify_event(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
  return FALSE;
}

static void _window_realize(GtkWidget *widget, gpointer data) {
  gdk_window_set_events(widget->window,
    gdk_window_get_events(widget->window) |
      GDK_BUTTON_PRESS_MASK |
      GDK_POINTER_MOTION_MASK);
}

void render_window(unsigned int id) {
  cairo_t *cr;

  if (id != 0 && id != 1) {
    return;
  }
  cr = gdk_cairo_create(windows[id].win->window);
  if (!cr) return;

  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_rectangle(cr, 0, 0, windows[id].cx, windows[id].cy);
  cairo_fill(cr);

  if (windows[id].render) {
    windows[id].render(cr, windows[id].cx, windows[id].cy);
  }

  cairo_destroy(cr);
}

static void render_presentation_window(cairo_t *cr, int width, int height) {
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

static void render_console_window(cairo_t *cr, int width, int height) {
  cairo_surface_t *page_surface;
  unsigned int w, h;
  unsigned int index;
  double scale, tmp;
  double page_offset = 0.0f;
  int guess_split;

  index = presentation_get_current_page();
  if (page_cache_fetch_page(index, &page_surface, &w, &h, &guess_split) != 0) {
    fprintf(stderr, "could not fetch page %d\n", index);
    return;
  }
  if ((guess_split && _config.force_notes == 0) || _config.force_notes == 1) {
    w /= 2;
    page_offset = -((double)w);
  }
  scale = ((double)width)/((double)w);
  tmp = ((double)height)/((double)h);
  if (tmp < scale) scale = tmp;
  scale *= 0.8f;

  cairo_scale(cr, scale, scale);

  cairo_set_source_surface(cr, page_surface, page_offset, 0.0f);
  cairo_rectangle(cr, 0.0f, 0.0f, w, h);
  cairo_fill(cr);
}

int main_window_to_page(unsigned int id, int wx, int wy, double *px, double *py) {
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
  if (id == 0 || (id == 1 && !_config.show_console)) {
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

int main_page_to_window(unsigned int id, double px, double py, int *wx, int *wy) {
  return 0;
}

static void page_action_callback(unsigned int action, void *data) {
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

void main_read_config(int argc, char **argv) {
  memset(&_config, 0, sizeof(struct _PresenterConfig));
  if (argc > 1) {
    _config.filename = g_strdup(argv[1]);
  }
  _config.show_console = 1;
  _config.scale_to_height = 768;
}

void main_quit(void) {
  gtk_main_quit();
}

void main_reconfigure_windows(void) {
  windows[0].render = render_presentation_window;
  if (_config.show_console) {
    windows[1].render = render_console_window;
  }  
  else {
    windows[1].render = render_presentation_window;
  }
}

void main_recalc_window_page_display(void) {
/* page_display, bounds, scale */
}

void config_step_notes(void) {
  if (_config.force_notes == 2) {
    _config.force_notes = 0;
  }
  else {
    _config.force_notes++;
  }
  main_reconfigure_windows();
}

void config_toggle_console(void) {
  _config.show_console = !_config.show_console;
  main_reconfigure_windows();
}

