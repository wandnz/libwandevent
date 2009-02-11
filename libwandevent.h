/*
 * This file is part of libwandevent
 *
 * Copyright (c) 2009 The University of Waikato, Hamilton, New Zealand.
 *
 * Authors: Perry Lorier
 *          Shane Alcock
 *
 * All rights reserved.
 *
 * This code has been developed by the University of Waikato WAND research
 * group. For further information, please see http://www.wand.net.nz/
 *
 * libwandevent is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License (GPL) as
 * published by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * libwandevent is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with libwandevent; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA  0211-1307  USA
 *
 * Any feedback (bug reports, suggestions, complaints) should be sent to
 * contact@wand.net.nz
 *
 * $Id$
 *
 */
#ifndef EVENT_H
#define EVENT_H
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum wand_eventtype_t { 
	EV_READ   = 1, 
	EV_WRITE  = 2, 
	EV_EXCEPT = 4
};

struct wand_fdcb_t {
	int fd;
	int flags;
	void *data;
	void (*callback)(struct wand_fdcb_t *handle, enum wand_eventtype_t ev);
};

struct wand_timer_t {
	struct timeval expire;
	void (*callback)(struct wand_timer_t *timer);
	void *data;
	struct wand_timer_t *prev;
	struct wand_timer_t *next;
};

struct wand_signal_t {
	int signum;
	void (*callback)(struct wand_signal_t *signal);
	void *data;
};

typedef struct wand_event_handler_t {
	fd_set rfd;		/* select fdsets */
	fd_set wfd;
	fd_set xfd;		
	
	struct wand_fdcb_t **fd_events;	
	struct wand_timer_t *timers;	
	struct wand_timer_t *timers_tail;

	int maxfd;
	bool walltimeok;
	struct timeval walltime;
	bool monotonictimeok;
	struct timeval monotonictime;
	bool running;

} wand_event_handler_t;

int wand_event_init(void);
wand_event_handler_t * wand_create_event_handler(void);
void wand_destroy_event_handler(wand_event_handler_t *wand_ev);
struct timeval wand_calc_expire(wand_event_handler_t *ev_hdl, int sec,int usec);
void wand_add_event(wand_event_handler_t *ev_hdl, struct wand_fdcb_t *);
void wand_add_timer(wand_event_handler_t *ev_hdl, struct wand_timer_t *);
void wand_add_signal(struct wand_signal_t *);
void wand_del_event(wand_event_handler_t *ev_hdl, struct wand_fdcb_t *);
void wand_del_timer(wand_event_handler_t *ev_hdl, struct wand_timer_t *);
void wand_del_signal(struct wand_signal_t *);
void wand_event_run(wand_event_handler_t *ev_hdl);
struct timeval wand_get_walltime(wand_event_handler_t *ev_hdl);
struct timeval wand_get_monotonictime(wand_event_handler_t *ev_hdl);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
