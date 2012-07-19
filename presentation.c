#include "presentation.h"
#include "page-cache.h"
#include <poppler.h>
#include "utils.h"
#include <string.h>
#include <stdio.h>

struct _Presentation {
  unsigned int current_index;
  unsigned int page_count;
  void (*action_cb)(unsigned int, void*);
  void *action_cb_data;
} _presentation;

void _presentation_call_action_cb(int action);

void presentation_init(void (*cb)(unsigned int, void*), void *userdata) {
  _presentation.page_count = page_cache_get_page_count();
  _presentation.current_index = 0;
  _presentation.action_cb = cb;
  _presentation.action_cb_data = userdata;
  page_cache_load_page(_presentation.current_index);
}

unsigned int presentation_get_current_page(void) {
  return _presentation.current_index;
}

void presentation_page_next(void) {
  if (_presentation.current_index < _presentation.page_count-1) {
    page_cache_page_unref(_presentation.current_index);
    page_cache_load_page(++_presentation.current_index);
    _presentation_call_action_cb(PRESENTATION_ACTION_PAGE_CHANGED);
  }
}

void presentation_page_prev(void) {
  if (_presentation.current_index > 0) {
    page_cache_page_unref(_presentation.current_index);
    page_cache_load_page(--_presentation.current_index);
    _presentation_call_action_cb(PRESENTATION_ACTION_PAGE_CHANGED);
  }
}

void presentation_page_first(void) {
  if (_presentation.current_index != 0) {
    page_cache_page_unref(_presentation.current_index);
    _presentation.current_index = 0;
    page_cache_load_page(_presentation.current_index);
    _presentation_call_action_cb(PRESENTATION_ACTION_PAGE_CHANGED);
  }
}

void presentation_page_last(void) {
  if (_presentation.current_index != _presentation.page_count-1) {
    page_cache_page_unref(_presentation.current_index);
    _presentation.current_index = _presentation.page_count-1;
    page_cache_load_page(_presentation.current_index);
    _presentation_call_action_cb(PRESENTATION_ACTION_PAGE_CHANGED);
  }
}

void presentation_page_goto(unsigned int index) {
  if (_presentation.current_index != index &&
      index <= _presentation.page_count -1) {
    page_cache_page_unref(_presentation.current_index);
    _presentation.current_index = index;
    page_cache_load_page(_presentation.current_index);
    _presentation_call_action_cb(PRESENTATION_ACTION_PAGE_CHANGED);
  }
}

void _presentation_call_action_cb(int action) {
  if (_presentation.action_cb) {
    _presentation.action_cb(action, _presentation.action_cb_data);
  }
}

void _perform_action_goto_dest(PopplerActionGotoDest *action);
void _perform_action_named(PopplerActionNamed *action);

int presentation_has_action_at(double x, double y) {
  if (page_cache_get_action_from_pos(x, y))
    return 1;
  return 0;
}

int presentation_perform_action_at(double x, double y) {
  PopplerAction *action = page_cache_get_action_from_pos(x, y);
  if (!action) {
    return 1;
  }
  switch (action->type) {
    default:
      fprintf(stderr, "Currently unhandled action: %d\n",
        action->type);
      break;
    case POPPLER_ACTION_GOTO_DEST:
      _perform_action_goto_dest((PopplerActionGotoDest*)action);
      break;
    case POPPLER_ACTION_NAMED:
      _perform_action_named((PopplerActionNamed*)action);
      break;
  }
  return 0;
}

void _perform_action_goto_dest(PopplerActionGotoDest *action) {
  PopplerDest *dest = NULL;
  int goto_index = 0;
  if (action->dest->type == POPPLER_DEST_NAMED) {
    dest = page_cache_get_named_dest(action->dest->named_dest);
    if (dest) {
      goto_index = dest->page_num-1;
      poppler_dest_free(dest);
    }
  }
  else {
    goto_index = action->dest->page_num-1;
  }
  presentation_page_goto(goto_index);
}

void _perform_action_named(PopplerActionNamed *action) {
  int actionid = 0;
  if (!strcmp(action->named_dest, "Find")) {
    actionid = PRESENTATION_ACTION_FIND;
  }
  else if (!strcmp(action->named_dest, "GoToPage")) {
    actionid = PRESENTATION_ACTION_GO_TO_PAGE;
  }
  else if (!strcmp(action->named_dest, "GoForward")) {
    actionid = PRESENTATION_ACTION_GO_FORWARD;
  }
  else if (!strcmp(action->named_dest, "GoBack")) {
    actionid = PRESENTATION_ACTION_GO_BACK;
  }
  else if (!strcmp(action->named_dest, "FirstPage")) {
    presentation_page_first();
  }
  else if (!strcmp(action->named_dest, "LastPage")) {
    presentation_page_last();
  }
  else if (!strcmp(action->named_dest, "PrevPage")) {
    presentation_page_prev();
  }
  else if (!strcmp(action->named_dest, "NextPage")) {
    presentation_page_next();
  }
  else if (!strcmp(action->named_dest, "Quit")) {
    actionid = PRESENTATION_ACTION_QUIT;
  }
  else {
    fprintf(stderr, "Unhandled named action: %s\n", action->named_dest);
    return;
  }
  if (actionid) {
    _presentation_call_action_cb(actionid);
  }
}

void presentation_get_status(PresentationStatus *status) {
  PageCacheStatus pcstate;
  if (status) {
    status->current_page = _presentation.current_index+1;
    status->num_pages = _presentation.page_count;
    page_cache_get_status(&pcstate);
    status->cached_pages = pcstate.pages_cached;
    status->cached_size = pcstate.cached_size;
  }
}


