/*
 * This file is part of wdcap
 *
 * Copyright (c) 2004-2006 The University of Waikato, Hamilton, New Zealand.
 * Authors: Daniel Lawson
 *          Shane Alcock
 *          Perry Lorier
 *
 * All rights reserved.
 *
 * This code has been developed by the University of Waikato WAND
 * research group. For further information please see http://www.wand.net.nz/
 *
 * This source code is proprietary to the University of Waikato
 * WAND research group and may not be redistributed, published or disclosed
 * without prior permission from WAND.
 *
 * Report any bugs, questions or comments to contact@wand.net.nz
 *
 * $Id: event.h 72 2006-06-23 03:38:58Z spa1 $
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
	//struct wand_signal_t *prev;
	//struct wand_signal_t *next;
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
//void Log(char *msg,...);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
