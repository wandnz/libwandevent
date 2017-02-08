/*
 * This file is part of libwandevent
 *
 * Copyright (c) 2009 The University of Waikato, Hamilton, New Zealand.
 *
 * Authors: Perry Lorier
 * 	    Shane Alcock
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
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Any feedback (bug reports, suggestions, complaints) should be sent to
 * contact@wand.net.nz
 *
 */

/* select() event loop */
#include "config.h"

#include <sys/select.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdarg.h>
#include <signal.h>
#include "libwandevent.h"
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <inttypes.h>

#include <pthread.h>

#if HAVE_SYS_EPOLL_H
 #include <sys/epoll.h>
 #include "epollhelper.h"
#else
 #include "selecthelper.h"
#endif

#ifndef EVENT_DEBUG
#define EVENT_DEBUG 0
#endif

/* We use global variables and a mutex to handle signal events, as it just
 * turns out to be a pain to deal with it all using the event handler
 * structure */
int signal_pipe[2];
int signal_pipe_fd = -1;
int signal_users = 0;
struct sigaction signal_event;
sigset_t active_sig;
struct sigaction default_sig;
int maxsig;
bool using_signals = false;

struct wand_signal_t **signals;

pthread_mutex_t signal_mutex;

/* This is our actual signal handler. All it does is write the signal number
 * into the signal pipe we created earlier. This will trigger an fd event on
 * the pipe, which we can use to trigger the event outside of the signal
 * interrupt (because doing things for any length of time inside a signal
 * interrupt is very bad!) */
static void event_sig_hdl(int signum) {
	if (write(signal_pipe[1], &signum, 4) != 4) {
		fprintf(stderr, "error writing signum to pipe\n");
	}
}

/* Callback function for an event on the signal pipe. If this event fires,
 * it means that a signal has occurred. The signal number will have been
 * written to the pipe, so we can just read it out and call the appropriate
 * callback for that signal number */
static void pipe_read(wand_event_handler_t *ev_hdl, int fd, void *data,
		enum wand_eventtype_t ev) {
	int signum = -1;
	int ret = -1;
	struct wand_signal_t *signal;

	assert(ev == EV_READ);
	assert(data == NULL);
	if ((ret = read(fd, &signum, 4)) != 4) {
		if (ret == -1 && errno == EAGAIN) {
			/* Another thread might have already read the signal */
			return;
		}

		fprintf(stderr, "error reading signum from pipe\n");
		return;
	}

	/* Don't let any threaded programs mess with our set of signal
	 * events while we're trying to invoke callbacks */
	pthread_mutex_lock(&signal_mutex);
	if (signum > maxsig) {
		fprintf(stderr, "signum %d > maxsig %d\n", signum, maxsig);
		pthread_mutex_unlock(&signal_mutex);
		return;
	}
	signal = signals[signum];
	/* Release the lock here - otherwise we will deadlock if a signal
	 * callback tries to delete the signal event or add a new one */
	pthread_mutex_unlock(&signal_mutex);

	if (signal != NULL) {
		signal->callback(ev_hdl, signum, signal->data);
	}
}

static void set_close_on_exec(int fd)
{
	int fileflags;
	if ((fileflags = fcntl(fd, F_GETFD, 0)) == -1) {
		return;
	}
	fcntl(fd, F_SETFD, fileflags | FD_CLOEXEC);
}

/* Primarily, this function initialises the signal pipe and the signal
 * handler, but it should always be called prior to doing any libwandevent
 * stuff. */
int wand_event_init() {
	int fileflags;
	sigemptyset(&(active_sig));

	pthread_mutex_init(&signal_mutex, NULL);

	signal_event.sa_handler = event_sig_hdl;
	sigemptyset(&(signal_event.sa_mask));
	signal_event.sa_flags = 0;

	default_sig.sa_handler = SIG_DFL;
	sigemptyset(&(default_sig.sa_mask));
	default_sig.sa_flags = 0;

	if (pipe(signal_pipe) != 0) {
		fprintf(stderr, "Error creating signal event pipe\n");
		return -1;
	}

	if ((fileflags = fcntl(signal_pipe[0], F_GETFL, 0)) == -1) {
		fprintf(stderr, "Failed to get flags for signal pipe\n");
		return -1;
	}

	if (fcntl(signal_pipe[0], F_SETFL,
			fileflags | O_NONBLOCK) == -1) {
		fprintf(stderr, "Failed to set flags for signal pipe\n");
		return -1;
	}

	set_close_on_exec(signal_pipe[0]);
	set_close_on_exec(signal_pipe[1]);


	signal_pipe_fd = signal_pipe[0];
	signals = NULL;
	maxsig = -1;
	using_signals = false;
	return 1;
}

/* Creates an event handler environment and initialises all the "global"
 * variables associated with it */
wand_event_handler_t * wand_create_event_handler()
{
	wand_event_handler_t *wand_ev;
	wand_ev = (wand_event_handler_t *)malloc(sizeof(wand_event_handler_t));

#if HAVE_SYS_EPOLL_H
	wand_ev->epoll_fd = epoll_create(100);
	if (wand_ev->epoll_fd < 0) {
		perror("epoll_create");
		fprintf(stderr, "Libwandevent failed to create epoll fd\n");
		free(wand_ev);
		return NULL;
	}
#else
	FD_ZERO(&(wand_ev->rfd));
	FD_ZERO(&(wand_ev->wfd));
	FD_ZERO(&(wand_ev->xfd));
#endif
	wand_ev->fd_events=NULL;
	wand_ev->timers=NULL;
	wand_ev->timers_tail=NULL;
	wand_ev->maxfd=-1;
	wand_ev->running=true;
	wand_ev->walltimeok=false;
	wand_ev->monotonictimeok=false;
	wand_ev->walltime.tv_sec=0;
	wand_ev->walltime.tv_usec=0;
	wand_ev->monotonictime.tv_sec=0;
	wand_ev->monotonictime.tv_usec=0;

	pthread_mutex_lock(&signal_mutex);
	signal_users ++;
	pthread_mutex_unlock(&signal_mutex);

	/* Add an event to watch for signals */
	assert(wand_add_fd(wand_ev, signal_pipe_fd, EV_READ, NULL, pipe_read));
	return wand_ev;
}

static void clear_timers(wand_event_handler_t *wand_ev) {
	struct wand_timer_t *tmp = wand_ev->timers;

	while (wand_ev->timers != NULL) {
		tmp = wand_ev->timers;
		wand_ev->timers = wand_ev->timers->next;
		free(tmp);
	}
	wand_ev->timers_tail = NULL;

}

static void clear_signals(wand_event_handler_t *wand_ev) {
	int i;

	assert(wand_ev);
	pthread_mutex_lock(&signal_mutex);
	signal_users --;

	if (signal_users > 0) {
		pthread_mutex_unlock(&signal_mutex);
		return;
	}

        /*
         * TODO Ideally this should use wand_del_signal but I don't feel like
         * dealing properly with the locking just now.
         */
	for (i = 0; i <= maxsig; i++) {
		if (signals[i])
			free(signals[i]);
	}
	free(signals);
	signals = NULL;
	pthread_mutex_unlock(&signal_mutex);
}

static void clear_fds(wand_event_handler_t *wand_ev) {
	int i;

	for (i = 0; i <= wand_ev->maxfd; i++) {
		if (wand_ev->fd_events[i])
			wand_del_fd(wand_ev, i);
	}
	free(wand_ev->fd_events);

}

/* Frees all the resources associated with an event handler */
void wand_destroy_event_handler(wand_event_handler_t *wand_ev) {

	clear_timers(wand_ev);

	if (signals) {
		clear_signals(wand_ev);
	}

	if (wand_ev->fd_events) {
		clear_fds(wand_ev);
	}

	if (wand_ev->epoll_fd >= 0)
		close(wand_ev->epoll_fd);

	free(wand_ev);
}

/* Returns a timeval that is sec.usec seconds from the current monotonic time.
 * This timeval can be plugged directly into a wand_timer_t to set up the
 * expiry time for a timer event */
struct timeval wand_calc_expire(wand_event_handler_t *ev_hdl, int sec,int usec)
{
	struct timeval tmp;
	tmp = wand_get_monotonictime(ev_hdl);
	tmp.tv_sec+=sec;
	tmp.tv_usec+=usec;
	if (tmp.tv_usec>=1000000) {
		tmp.tv_sec+=1;
		tmp.tv_usec-=1000000;
	}
	return tmp;
}


/* Registers a new signal event. */
struct wand_signal_t *wand_add_signal(int signum, void *data,
		void (*callback)(wand_event_handler_t *ev_hdl, int signum,
				void *data))
{
	struct wand_signal_t *siglist;
	struct wand_signal_t *signal;

	/* Don't forget to grab the mutex, because we're not thread-safe */
	pthread_mutex_lock(&signal_mutex);
	assert(signum>0);

	signal = (struct wand_signal_t *)malloc(sizeof(struct wand_signal_t));
	signal->signum = signum;
	signal->data = data;
	signal->callback = callback;
	using_signals = true;

        if (signal->signum > maxsig) {
                signals=realloc(signals,
			sizeof(struct wand_signal_t)*(signal->signum+1));
                /* FIXME: Deal with OOM */
                while(signal->signum > maxsig) {
                        signals[++maxsig]=NULL;
                }
                maxsig=signal->signum;
        }

	siglist = signals[signal->signum];
	if (siglist == NULL) {
		sigaddset(&active_sig, signal->signum);
		if (sigaction(signal->signum, &signal_event, 0) < 0) {
			printf("Error adding sigaction\n");
		}
		sigprocmask(SIG_BLOCK, &active_sig, 0);
		signals[signal->signum] = signal;
	} else {
		/* This signal already has a callback for it */
		return NULL;

	}

	pthread_mutex_unlock(&signal_mutex);
	return signal;
}

/* Cancels a signal event */
void wand_del_signal(int signum)
{
	sigset_t removed;
	struct wand_signal_t *signal = signals[signum];

	pthread_mutex_lock(&signal_mutex);

	if (signals[signal->signum] != NULL) {
		sigdelset(&active_sig, signum);
		sigemptyset(&removed);
		sigaddset(&removed, signum);
		if (sigaction(signum, &default_sig, 0) < 0) {
			fprintf(stderr, "Error removing sigaction\n");
		}
		sigprocmask(SIG_UNBLOCK, &removed, 0);
		free(signal);
		signals[signum] = NULL;
	} else {
		/* No signal here? */

	}

	pthread_mutex_unlock(&signal_mutex);
}

#define TV_CMP(a,b) ((a).tv_sec == (b).tv_sec 		\
			? (a).tv_usec - (b).tv_usec 	\
			: (a).tv_sec - (b).tv_sec)

/* Registers a timer event */
struct wand_timer_t *wand_add_timer(wand_event_handler_t *ev_hdl,
		int sec, int usec, void *data,
		void (*callback)(wand_event_handler_t *ev_hdl, void *data))
{

	struct wand_timer_t *timer;
	struct wand_timer_t *tmp = ev_hdl->timers_tail;

	if (sec < 0 || usec < 0 || usec >= 1000000) {
		fprintf(stderr, "Libwandevent: invalid expiry parameters: %d %d\n", sec, usec);
		return NULL;
	}


	timer = (struct wand_timer_t *)malloc(sizeof(struct wand_timer_t));
	timer->expire = wand_calc_expire(ev_hdl, sec, usec);
	timer->callback = callback;
	timer->data = data;
	timer->prev = timer->next = NULL;

	if (ev_hdl->timers==NULL) {
		ev_hdl->timers_tail=ev_hdl->timers=timer;
		return timer;
	}
	assert(ev_hdl->timers_tail->next == NULL);

	/* Doubly linked lists are annoying! */
	/* FIXME: This code sucks ass */
	while(tmp->prev) {

		if (TV_CMP(tmp->expire, timer->expire) <= 0) {
			/* insert */
			if (tmp->next)
				tmp->next->prev = timer;
			else
				ev_hdl->timers_tail = timer;
			timer->next = tmp->next;
			timer->prev = tmp;
			tmp->next = timer;
			return timer;
		}
		tmp = tmp->prev;
	}

	if (TV_CMP(tmp->expire, timer->expire) < 0) {
		if (tmp->next)
                	tmp->next->prev = timer;
                else
                        ev_hdl->timers_tail = timer;
                timer->next = tmp->next;
                timer->prev = tmp;
                tmp->next = timer;
	} else {
		tmp->prev = timer;
		timer->next = tmp;
		timer->prev = NULL;
		ev_hdl->timers = timer;
	}
	return timer;
}

static void dump_timers(wand_event_handler_t *ev_hdl) {

	struct wand_timer_t *t = ev_hdl->timers;
	int c = 0;

	while(t) {
		fprintf(stderr, "%u.%u ", (uint32_t)t->expire.tv_sec,
				(uint32_t)t->expire.tv_usec);
		t = t->next;
		c++;

		assert(c<5);
	}

	fprintf(stderr, "\n");

}

/* Cancels a timer event */
void wand_del_timer(wand_event_handler_t *ev_hdl, struct wand_timer_t *timer)
{
	assert(timer->prev!=(void*)0xdeadbeef);
	assert(timer->next!=(void*)0xdeadbeef);
	if (timer->prev)
		timer->prev->next=timer->next;
	else
		ev_hdl->timers=timer->next;
	if (timer->next)
		timer->next->prev=timer->prev;

	if (ev_hdl->timers_tail == timer) {
		ev_hdl->timers_tail = timer->prev;
	}

	free(timer);
}

/* Adds a file descriptor event */
struct wand_fdcb_t * wand_add_fd(wand_event_handler_t *ev_hdl,
		int fd, int flags, void *data,
		void (*callback)(wand_event_handler_t *ev_hdl,
				int fd, void *data, enum wand_eventtype_t ev))
{

	struct wand_fdcb_t *evcb;

	if (fd < 0)
		return NULL;

	if (fd < ev_hdl->maxfd) {
		if (ev_hdl->fd_events[fd] != NULL) {
			fprintf(stderr, "Libwandevent fd event already exists for fd %d\n", fd);
			return NULL;
		}
	}

	evcb = (struct wand_fdcb_t *)malloc(sizeof(struct wand_fdcb_t));
	evcb->fd = fd;
	evcb->flags = flags;
	evcb->data = data;
	evcb->callback = callback;

	if (evcb->fd>ev_hdl->maxfd) {
		ev_hdl->fd_events=realloc(ev_hdl->fd_events,
				sizeof(struct wand_fdcb_t)*(evcb->fd+1));
		/* FIXME: Deal with OOM */
		while(ev_hdl->maxfd<evcb->fd) {
			ev_hdl->fd_events[++(ev_hdl->maxfd)]=NULL;
		}
		ev_hdl->maxfd=evcb->fd;
	}
	ev_hdl->fd_events[evcb->fd]=evcb;

#if HAVE_SYS_EPOLL_H
	evcb->internal = create_epoll_event(ev_hdl, fd, flags);
	if (evcb->internal == NULL) {
		free(evcb);
		return NULL;
	}
#else
	if (evcb->flags & EV_READ)   FD_SET(evcb->fd,&(ev_hdl->rfd));
	if (evcb->flags & EV_WRITE)  FD_SET(evcb->fd,&(ev_hdl->wfd));
	if (evcb->flags & EV_EXCEPT) FD_SET(evcb->fd,&(ev_hdl->xfd));
#endif

#if EVENT_DEBUG
	printf("New events for %d:",evcb->fd);
	if (evcb->flags & EV_READ) printf(" read");
	if (evcb->flags & EV_WRITE) printf(" write");
	if (evcb->flags & EV_EXCEPT) printf(" except");
	printf("\n");
#endif

	return evcb;
}

int wand_get_fd_flags(wand_event_handler_t *ev_hdl, int fd) {
	struct wand_fdcb_t *evcb;
	assert(fd>=0);

	if (fd > ev_hdl->maxfd || ev_hdl->fd_events[fd] == NULL)
		return -1;
	evcb = ev_hdl->fd_events[fd];
	assert(evcb->fd == fd);

	return evcb->flags;
}

void wand_set_fd_flags(wand_event_handler_t *ev_hdl, int fd, int new_flags) {
	struct wand_fdcb_t *evcb;
	assert(fd>=0);

	if (fd > ev_hdl->maxfd || ev_hdl->fd_events[fd] == NULL)
		return;
	evcb = ev_hdl->fd_events[fd];
	assert(evcb->fd == fd);

	evcb->flags = new_flags;

#if HAVE_SYS_EPOLL_H
	set_epoll_event((struct epoll_event *)evcb->internal, fd, evcb->flags);
	int ret = epoll_ctl(ev_hdl->epoll_fd, EPOLL_CTL_MOD, fd,
			(struct epoll_event *)evcb->internal);
	if (ret < 0) {
		perror("epoll_ctl");
		fprintf(stderr, "Error modifying fd %d within epoll\n", fd);
		return;
	}
#else
	FD_CLR(fd,&(ev_hdl->rfd));
	FD_CLR(fd,&(ev_hdl->wfd));
	FD_CLR(fd,&(ev_hdl->xfd));
	if (evcb->flags & EV_READ)   FD_SET(evcb->fd,&(ev_hdl->rfd));
	if (evcb->flags & EV_WRITE)  FD_SET(evcb->fd,&(ev_hdl->wfd));
	if (evcb->flags & EV_EXCEPT) FD_SET(evcb->fd,&(ev_hdl->xfd));
#endif
}

/* Cancels a file descriptor event */
void wand_del_fd(wand_event_handler_t *ev_hdl, int fd)
{
	struct wand_fdcb_t *evcb;
	assert(fd>=0);

	if (fd > ev_hdl->maxfd || ev_hdl->fd_events[fd] == NULL)
		return;
	evcb = ev_hdl->fd_events[fd];
	assert(evcb->fd == fd);

	ev_hdl->fd_events[fd]=NULL;
#if HAVE_SYS_EPOLL_H
	int ret = epoll_ctl(ev_hdl->epoll_fd, EPOLL_CTL_DEL, fd,
			(struct epoll_event *)evcb->internal);
	if (ret < 0) {
		perror("epoll_ctl");
		fprintf(stderr, "Error removing fd %d from epoll (epollfd=%d)\n", fd, ev_hdl->epoll_fd);
		return;
	}
	free(evcb->internal);
#else
	if (evcb->flags & EV_READ)   FD_CLR(fd,&(ev_hdl->rfd));
	if (evcb->flags & EV_WRITE)  FD_CLR(fd,&(ev_hdl->wfd));
	if (evcb->flags & EV_EXCEPT) FD_CLR(fd,&(ev_hdl->xfd));
#endif

#if EVENT_DEBUG
	printf("del events for %d\n",evcb->fd);
#endif

	free(evcb);
}

#define NEXT_TIMER ev_hdl->timers

/* Since requiring the walltime now is optional (you probably want to be using
 * the monotonic clock for stuff), we only update it when we need it.
 */
struct timeval wand_get_walltime(wand_event_handler_t *ev_hdl)
{
	if (!ev_hdl->walltimeok) {
		gettimeofday(&ev_hdl->walltime,NULL);
		ev_hdl->walltimeok=true;
	}

	return ev_hdl->walltime;
}

struct timeval wand_get_monotonictime(wand_event_handler_t *ev_hdl)
{
#if defined _POSIX_MONOTONIC_CLOCK && (_POSIX_MONOTONIC_CLOCK > -1)
	struct timespec ts;
	if (!ev_hdl->monotonictimeok) {
		clock_gettime(CLOCK_MONOTONIC,&ts);
		ev_hdl->monotonictime.tv_sec = ts.tv_sec;
		/* Convert from nanoseconds to microseconds */
		ev_hdl->monotonictime.tv_usec = ts.tv_nsec/1000;
		ev_hdl->monotonictimeok=true;
	}
	return ev_hdl->monotonictime;
#else
#warning "No monotonic clock support on this system"
	ev_hdl->monotonictime = wand_get_walltime(ev_hdl);
	return ev_hdl->monotonictime;
#endif
}

#define MAX_EVENTS 64

/* Starts up the event handler. Essentially, the event handler will loop
 * infinitely until an error occurs or an event callback sets the running
 * variable to false.
 */
void wand_event_run(wand_event_handler_t *ev_hdl)
{
	struct wand_timer_t *tmp = 0;
	sigset_t current_sig;

#if HAVE_SYS_EPOLL_H
	struct epoll_event epoll_evs[MAX_EVENTS];
	int fdevents = 0;
	int ms_delay;
	int i;
#else
	fd_set xrfd, xwfd, xxfd;
	int retval;
	int fd;
	struct timeval delay;
	struct timeval *delayp;
#endif


	while (ev_hdl->running) {
		pthread_mutex_lock(&signal_mutex);
		current_sig = active_sig;
		pthread_mutex_unlock(&signal_mutex);

		/* Force the monotonic clock up to date */
		wand_get_monotonictime(ev_hdl);


		/* Check for timer events that have fired */
		while(NEXT_TIMER &&
			TV_CMP(ev_hdl->monotonictime, NEXT_TIMER->expire)>0)
		{
			assert(NEXT_TIMER->prev == NULL);
			tmp=NEXT_TIMER;
			if (NEXT_TIMER->next) {
				NEXT_TIMER->next->prev = NEXT_TIMER->prev;
			}
			NEXT_TIMER=NEXT_TIMER->next;
			if (NEXT_TIMER == NULL)
				ev_hdl->timers_tail=NULL;
			tmp->prev=(void*)0xdeadbeef;
			tmp->next=(void*)0xdeadbeef;
#if EVENT_DEBUG
			fprintf(stderr,"Timer expired\n");
#endif
			tmp->callback(ev_hdl, tmp->data);
			free(tmp);
			if (!ev_hdl->running)
				return;
		}

		/* We want our upcoming select() to finish before the next
		 * timer event is due to fire */
#if HAVE_SYS_EPOLL_H
		if (NEXT_TIMER)
			ms_delay = calculate_epoll_delay(ev_hdl, NEXT_TIMER);
		else
			ms_delay = -1;
#else
		if (NEXT_TIMER) {
			delay = calculate_select_delay(ev_hdl, NEXT_TIMER);
			delayp = &delay;
		} else {
			delayp = NULL;
		}
#endif

		/* Because we can handle our signal events via an fd event,
		 * we only want to allow signal interrupts while we're
		 * capable of triggering fd events. */
		if (using_signals) {
			sigprocmask(SIG_UNBLOCK, &current_sig, 0);
		}


#if HAVE_SYS_EPOLL_H
		do {
			fdevents = epoll_wait(ev_hdl->epoll_fd,
					epoll_evs, MAX_EVENTS, ms_delay);
			if (fdevents == -1 && errno != EINTR) {
				perror("epoll_wait");
				fprintf(stderr, "Libwandevent: error in epoll\n");
				return;
			}
		} while (fdevents == -1);
#else
		xrfd = ev_hdl->rfd;
		xwfd = ev_hdl->wfd;
		xxfd = ev_hdl->xfd;

		/* This select will wait for the next fd event to occur, or
		 * for the next timer to be ready to fire */
		do {
			retval = select(ev_hdl->maxfd+1,
					&xrfd,&xwfd,&xxfd,delayp);
			if (retval == -1 && errno != EINTR) {
				/* ERROR */
				printf("Error in select\n");
				return;
			}
		} while (retval == -1);
#endif

		/* Invalidate the clocks */
		ev_hdl->walltimeok=false;
		ev_hdl->monotonictimeok=false;

		/* Block all signal interrupts for signals that we are
		 * handling again */
		if (using_signals) {
			sigprocmask(SIG_BLOCK, &current_sig, 0);
		}

#if HAVE_SYS_EPOLL_H
		for (i = 0; i < fdevents; i++) {
			process_epoll_event(ev_hdl, &epoll_evs[i]);
		}
#else
		for(fd=0;fd<=ev_hdl->maxfd && retval>0;++fd) {
			/* Skip fd's we don't have events for */
			if (!ev_hdl->fd_events[fd])
				continue;
			assert(ev_hdl->fd_events[fd]->fd==fd);
			process_select_event(ev_hdl, fd, &xrfd, &xwfd, &xxfd);
		}
#endif
	}
}
