#ifndef SELECTHELPER_H_
#define SELECTHELPER_H_

#include "libwandevent.h"

void process_select_event(wand_event_handler_t *ev_hdl,
		int fd, fd_set *xrfd, fd_set *xwfd, fd_set *xxfd);
struct timeval calculate_select_delay(wand_event_handler_t *ev_hdl,
		struct wand_timer_t *next);

#endif
