#ifndef EPOLLHELPER_H_
#define EPOLLHELPER_H_

#include <sys/epoll.h>
#include "libwandevent.h"

void set_epoll_event(struct epoll_event *epev, int fd, int flags);
struct epoll_event *create_epoll_event(wand_event_handler_t *ev_hdl,
		int fd, int flags);
void process_epoll_event(wand_event_handler_t *ev_hdl, struct epoll_event *ev);
int calculate_epoll_delay(wand_event_handler_t *ev_hdl, 
		struct wand_timer_t *next);

#endif
