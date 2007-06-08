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

enum eventtype_t { 
	EV_READ   = 1, 
	EV_WRITE  = 2, 
	EV_EXCEPT = 4
};

struct fdcb_t {
	int fd;
	int flags;
	void *data;
	void (*callback)(struct fdcb_t *handle, enum eventtype_t ev);
};

struct timer_t {
	struct timeval expire;
	void (*callback)(struct timer_t *timer);
	void *data;
	struct timer_t *prev;
	struct timer_t *next;
};

struct signal_t {
	int signum;
	void (*callback)(struct signal_t *signal);
	void *data;
	struct signal_t *prev;
	struct signal_t *next;
};

int init_event(void);
struct timeval calc_expire(int sec,int usec);
void add_event(struct fdcb_t *);
void add_timer(struct timer_t *);
void add_signal(struct signal_t *);
void del_event(struct fdcb_t *);
void del_timer(struct timer_t *);
void del_signal(struct signal_t *);
void event_run(void);
void Log(char *msg,...);

extern bool running;
extern struct timeval now;

#endif
