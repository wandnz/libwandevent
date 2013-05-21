#include <sys/select.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include <sys/ioctl.h>

#include "selecthelper.h"

void process_select_event(wand_event_handler_t *ev_hdl,
                int fd, fd_set *xrfd, fd_set *xwfd, fd_set *xxfd) {
        /* This code makes me feel dirty */
        if ((ev_hdl->fd_events[fd]->flags & EV_READ) && FD_ISSET(fd,xrfd)) {
                int data;
                do {
                        ev_hdl->fd_events[fd]->callback(ev_hdl, fd,
                                        ev_hdl->fd_events[fd]->data, EV_READ);
                } while (ev_hdl->fd_events[fd] &&
                                ioctl(fd,FIONREAD,&data)>=0
                                && data>0);
                if (!ev_hdl->fd_events[fd])
                        return;
        }
        if ((ev_hdl->fd_events[fd]->flags & EV_WRITE)
                        && FD_ISSET(fd,xwfd)) {
                ev_hdl->fd_events[fd]->callback(
                                ev_hdl, fd,
                                ev_hdl->fd_events[fd]->data,
                                EV_WRITE);
                if (!ev_hdl->fd_events[fd])
                        return;
        }
        if ((ev_hdl->fd_events[fd]->flags & EV_EXCEPT)
                        && FD_ISSET(fd,xxfd)) {
                ev_hdl->fd_events[fd]->callback(
                                ev_hdl, fd,
                                ev_hdl->fd_events[fd]->data,
                                EV_EXCEPT);
        }
}

struct timeval calculate_select_delay(wand_event_handler_t *ev_hdl,
                struct wand_timer_t *next) {

        struct timeval delay;

	assert(next);
	delay.tv_sec = next->expire.tv_sec - ev_hdl->monotonictime.tv_sec;
	delay.tv_usec = next->expire.tv_usec - ev_hdl->monotonictime.tv_usec;
	if (delay.tv_usec<0) {
		delay.tv_usec += 1000000;
		--delay.tv_sec;
	}
	return delay;
}


