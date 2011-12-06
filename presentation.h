#ifndef __PRESENTATION_H__
#define __PRESENTATION_H__

#include <glib.h>
#include <cairo.h>

#define PRESENTATION_ACTION_PAGE_CHANGED                 1
#define PRESENTATION_ACTION_FIND                         2
#define PRESENTATION_ACTION_GO_TO_PAGE                   3
#define PRESENTATION_ACTION_GO_FORWARD                   4
#define PRESENTATION_ACTION_GO_BACK                      5
#define PRESENTATION_ACTION_FIRST_PAGE                   6
#define PRESENTATION_ACTION_LAST_PAGE                    7
#define PRESENTATION_ACTION_PREV_PAGE                    8
#define PRESENTATION_ACTION_NEXT_PAGE                    9
#define PRESENTATION_ACTION_QUIT                        10

void presentation_init(
                void (*cb)(unsigned int, void*), 
                void *userdata);
unsigned int presentation_get_current_page(void);
void presentation_page_next(void);
void presentation_page_prev(void);
void presentation_page_first(void);
void presentation_page_last(void);
void presentation_page_goto(unsigned int index);
int presentation_perform_action_at(double x, double y);

#endif

