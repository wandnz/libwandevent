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

/* File descriptor event */
struct wand_fdcb_t {
	/* The file descriptor that the event is registered on */
	int fd;
	/* The type of event(s) that we want to fire on for this fd */
	int flags;
	/* Pointer to data that can be accessed during the callback */
	void *data;
	/* Function to call when the event fires */
	void (*callback)(struct wand_fdcb_t *handle, enum wand_eventtype_t ev);
};

/* Timer event */
struct wand_timer_t {
	/* Time that the event is due to fire */
	struct timeval expire;
	/* Function to call when the event fires */
	void (*callback)(struct wand_timer_t *timer);
	/* Pointer to data that can be accessed during the callback */
	void *data;
	
	/* Timer events are stored in a doubly-linked list - these are the
	 * links. DO NOT touch these unless you want your timers to break! */
	struct wand_timer_t *prev;
	struct wand_timer_t *next;
};

/* Signal event */
struct wand_signal_t {
	/* The number of the signal the event is registered on */
	int signum;
	/* Function to call when the event fires */
	void (*callback)(struct wand_signal_t *signal);
	/* Pointer to data that can be accessed during the callback */
	void *data;
};

/* The event handler environment - essentially holds the "global" variables
 * for a libwandevent instance */
typedef struct wand_event_handler_t {
	/* fd sets for reading, writing and exception events, respectively */
	fd_set rfd;		
	fd_set wfd;
	fd_set xfd;		
	
	/* The currently active file descriptor events */
	struct wand_fdcb_t **fd_events;	
	/* Start of the timer event linked list */
	struct wand_timer_t *timers;	
	/* End of the timer event linked list */
	struct wand_timer_t *timers_tail;

	/* Highest file descriptor in any of the fd sets */
	int maxfd;
	/* Has the wall time been updated recently? */
	bool walltimeok;
	/* Current value for the wall time */
	struct timeval walltime;
	/* Has the monotonic time been updated recently? */
	bool monotonictimeok;
	/* Current value for the monotonic time */
	struct timeval monotonictime;

	/* If false, the handler will stop checking for events and return
	 * control to the user program */
	bool running;

} wand_event_handler_t;

/* Initialises libwandevent, particularly the signal handling */
int wand_event_init(void);

/* Creates and initialises a new event handler environment */
wand_event_handler_t * wand_create_event_handler(void);

/* Destroys and frees an event handler environment */
void wand_destroy_event_handler(wand_event_handler_t *wand_ev);

/* Given a number of seconds, calculates the timeval needed for a timer event 
 * to fire that far into the future. */
struct timeval wand_calc_expire(wand_event_handler_t *ev_hdl, int sec,int usec);

/* Registers a file descriptor event */
void wand_add_event(wand_event_handler_t *ev_hdl, struct wand_fdcb_t *);

/* Registers a timer event */
void wand_add_timer(wand_event_handler_t *ev_hdl, struct wand_timer_t *);

/* Registers a signal event */
void wand_add_signal(struct wand_signal_t *);

/* Cancels a file descriptor event */
void wand_del_event(wand_event_handler_t *ev_hdl, struct wand_fdcb_t *);

/* Cancels a timer event */
void wand_del_timer(wand_event_handler_t *ev_hdl, struct wand_timer_t *);

/* Cancels a signal event */
void wand_del_signal(struct wand_signal_t *);

/* Starts the event handler - at this point, the execution of your program 
 * will now only occur via the callback functions for the events you registered
 * prior to calling this function */
void wand_event_run(wand_event_handler_t *ev_hdl);

/* Returns the current walltime */
struct timeval wand_get_walltime(wand_event_handler_t *ev_hdl);

/* Returns the current monotonic time */
struct timeval wand_get_monotonictime(wand_event_handler_t *ev_hdl);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
