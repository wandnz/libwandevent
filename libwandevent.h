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
	struct wand_signal_t *prev;
	struct wand_signal_t *next;
};

int wand_init_event(void);
struct timeval wand_calc_expire(int sec,int usec);
void wand_add_event(struct wand_fdcb_t *);
void wand_add_timer(struct wand_timer_t *);
void wand_add_signal(struct wand_signal_t *);
void wand_del_event(struct wand_fdcb_t *);
void wand_del_timer(struct wand_timer_t *);
void wand_del_signal(struct wand_signal_t *);
void wand_event_run(void);
void Log(char *msg,...);

extern bool wand_event_running;
extern struct timeval wand_now;

#endif
