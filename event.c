/* select() event loop */
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

#include <pthread.h>

#ifndef EVENT_DEBUG
#define EVENT_DEBUG 0
#endif

int signal_pipe[2];
struct sigaction signal_event;
sigset_t active_sig;
struct wand_fdcb_t signal_pipe_event;
struct sigaction default_sig;
int maxsig;
bool using_signals;

struct wand_signal_t **signals;

pthread_mutex_t signal_mutex;

static void event_sig_hdl(int signum) {
	if (write(signal_pipe[1], &signum, 4) != 4) {
		fprintf(stderr, "error writing signum to pipe\n");
	}
}

static void pipe_read(struct wand_fdcb_t* evcb, enum wand_eventtype_t ev) {
	int signum = -1;
	int ret = -1;
	struct wand_signal_t *signal;
	if ((ret = read(signal_pipe[0], &signum, 4)) != 4) {
		if (ret == -1 && errno == EAGAIN) {
			/* Another thread might have already read the signal */
			return;
		}
			
		fprintf(stderr, "error reading signum from pipe\n");
		return;
	}
	
	pthread_mutex_lock(&signal_mutex);
	if (signum > maxsig) {
		fprintf(stderr, "signum %d > maxsig %d\n", signum, maxsig);
		pthread_mutex_unlock(&signal_mutex);
		return;
	}
	signal = signals[signum];
	if (signal != NULL) {
		signal->callback(signal);
	}
	pthread_mutex_unlock(&signal_mutex);
}

int wand_event_init() {
	int fileflags;
	sigemptyset(&(active_sig));
	
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

	if (fcntl(signal_pipe[0], F_SETFL, fileflags | O_NONBLOCK) == -1) {
		fprintf(stderr, "Failed to set flags for signal pipe\n");
		return -1;
	}
	
	signal_pipe_event.fd = signal_pipe[0];
	signal_pipe_event.flags = EV_READ;
	signal_pipe_event.callback = pipe_read;
	signal_pipe_event.data = 0;

	signals = NULL;
	maxsig = -1;
	using_signals = false;
	return 1;
}
	

wand_event_handler_t * wand_create_event_handler()
{
	wand_event_handler_t *wand_ev;
	wand_ev = (wand_event_handler_t *)malloc(sizeof(wand_event_handler_t));
	
	FD_ZERO(&(wand_ev->rfd));
	FD_ZERO(&(wand_ev->wfd));
	FD_ZERO(&(wand_ev->xfd));
	
	wand_ev->fd_events=NULL;
	wand_ev->timers=NULL;
	wand_ev->timers_tail=NULL;
	wand_ev->maxfd=-1;
	wand_ev->running=true;
	wand_ev->walltimeok=false;
	wand_ev->monotonictimeok=false;
	
	/* Add an event to watch for signals */
	wand_add_event(wand_ev, &(signal_pipe_event));
	return wand_ev;
}

void wand_destroy_event_handler(wand_event_handler_t *wand_ev) {
	
	if (wand_ev->fd_events)
		free(wand_ev->fd_events);
	
	free(wand_ev);
}

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


void wand_add_signal(struct wand_signal_t *signal)
{
	struct wand_signal_t *siglist;
	
	pthread_mutex_lock(&signal_mutex);
	assert(signal->signum>0);

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

	}
	
	pthread_mutex_unlock(&signal_mutex);
}

void wand_del_signal(struct wand_signal_t *signal) 
{
	sigset_t removed;
	
	pthread_mutex_lock(&signal_mutex);

	if (signals[signal->signum] != NULL) {
		sigdelset(&active_sig, signal->signum);
		sigaddset(&removed, signal->signum);
		if (sigaction(signal->signum, &default_sig, 0) < 0) {
			fprintf(stderr, "Error removing sigaction\n");
		}
		sigprocmask(SIG_UNBLOCK, &removed, 0);
	} else {
		/* No signal here? */

	}
	
	pthread_mutex_unlock(&signal_mutex);
}

#define TV_CMP(a,b) ((a).tv_sec == (b).tv_sec 		\
			? (a).tv_usec - (b).tv_usec 	\
			: (a).tv_sec - (b).tv_sec)

void wand_add_timer(wand_event_handler_t *ev_hdl, struct wand_timer_t *timer)
{
	struct wand_timer_t *tmp = ev_hdl->timers_tail;
	assert(timer->expire.tv_sec>=0);
	assert(timer->expire.tv_usec>=0);
	assert(timer->expire.tv_usec<1000000);
	if (ev_hdl->timers==NULL) {
		timer->prev = timer->next = NULL;
		ev_hdl->timers_tail=ev_hdl->timers=timer;
		return;
	}
	assert(timer->prev == NULL);
	assert(ev_hdl->timers_tail->next == NULL);
	
	/* Doubly linked lists are annoying! */
	/* FIXME: This code sucks ass */
	while(tmp->prev) {
		if (TV_CMP(tmp->expire, timer->expire) < 0) {
			/* insert */
			if (tmp->next)
				tmp->next->prev = timer;
			else 
				ev_hdl->timers_tail = timer;
			timer->next = tmp->next;
			timer->prev = tmp;
			tmp->next = timer;
			return;
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

}

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
}

void wand_add_event(wand_event_handler_t *ev_hdl, struct wand_fdcb_t *evcb)
{
	assert(evcb->fd>=0);
	/* can't add twice*/
	assert(evcb->fd>=ev_hdl->maxfd || ev_hdl->fd_events[evcb->fd]==NULL); 

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
	if (evcb->flags & EV_READ)   FD_SET(evcb->fd,&(ev_hdl->rfd));
	if (evcb->flags & EV_WRITE)  FD_SET(evcb->fd,&(ev_hdl->wfd));
	if (evcb->flags & EV_EXCEPT) FD_SET(evcb->fd,&(ev_hdl->xfd));
#if EVENT_DEBUG
	printf("New events for %d:",evcb->fd);
	if (evcb->flags & EV_READ) printf(" read");
	if (evcb->flags & EV_WRITE) printf(" write");
	if (evcb->flags & EV_EXCEPT) printf(" except");
	printf("\n");
#endif
}

void wand_del_event(wand_event_handler_t *ev_hdl, struct wand_fdcb_t *evcb)
{
	assert(evcb->fd>=0);
	assert(evcb->fd<=ev_hdl->maxfd && ev_hdl->fd_events[evcb->fd]!=NULL);
	ev_hdl->fd_events[evcb->fd]=NULL;
	if (evcb->flags & EV_READ)   FD_CLR(evcb->fd,&(ev_hdl->rfd));
	if (evcb->flags & EV_WRITE)  FD_CLR(evcb->fd,&(ev_hdl->wfd));
	if (evcb->flags & EV_EXCEPT) FD_CLR(evcb->fd,&(ev_hdl->xfd));
#if EVENT_DEBUG
	printf("del events for %d\n",evcb->fd);
#endif
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
#ifdef _POSIX_MONOTONIC_CLOCK
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
	return wand_get_wallclocktime();
#endif
}

void wand_event_run(wand_event_handler_t *ev_hdl)
{
	struct wand_timer_t *tmp = 0;
	struct timeval delay;
	int fd;
	int retval;
	fd_set xrfd, xwfd, xxfd;
	struct timeval *delayp;
	sigset_t current_sig;
	struct timespec monotime;
	
	while (ev_hdl->running) {
		pthread_mutex_lock(&signal_mutex);
		current_sig = active_sig;
		pthread_mutex_unlock(&signal_mutex);

		
		/* Invalidate the clocks */
		ev_hdl->walltimeok=false;
		ev_hdl->monotonictimeok=false;
		/* Force the monotonic clock up to date */
		wand_get_monotonictime(ev_hdl);
		/* Expire old timers */
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
			tmp->callback(tmp);
			if (!ev_hdl->running)
				return;
		}
		
		if (NEXT_TIMER) {
			delay.tv_sec = NEXT_TIMER->expire.tv_sec - ev_hdl->monotonictime.tv_sec;
			delay.tv_usec = NEXT_TIMER->expire.tv_usec - ev_hdl->monotonictime.tv_usec;
			if (delay.tv_usec<0) {
				delay.tv_usec += 1000000;
				--delay.tv_sec;
			}

			delayp = &delay;
		}
		else {
			delayp = NULL;
		}

		xrfd = ev_hdl->rfd;
		xwfd = ev_hdl->wfd;
		xxfd = ev_hdl->xfd;

		if (using_signals) {
			sigprocmask(SIG_UNBLOCK, &current_sig, 0);
		}
		
		do {
			retval = select(ev_hdl->maxfd+1,
					&xrfd,&xwfd,&xxfd,delayp);
			if (retval == -1 && errno != EINTR) {
				/* ERROR */
				printf("Error in select\n");
				return;
			}
		} while (retval == -1);

		if (using_signals) {
			sigprocmask(SIG_BLOCK, &current_sig, 0);
		}

		/* TODO: check select's return */
		for(fd=0;fd<=ev_hdl->maxfd && retval>0;++fd) {
			/* Skip fd's we don't have events for */
			if (!ev_hdl->fd_events[fd])
				continue;
			assert(ev_hdl->fd_events[fd]->fd==fd);
			/* This code makes me feel dirty */
			if ((ev_hdl->fd_events[fd]->flags & EV_READ) 
					&& FD_ISSET(fd,&xrfd)) {
				int data;
				do {
					ev_hdl->fd_events[fd]->callback(
							ev_hdl->fd_events[fd],
							EV_READ);
				} while (ev_hdl->fd_events[fd] && 
						ioctl(fd,FIONREAD,&data)>=0 
						&& data>0);
				--retval;
				if (!ev_hdl->fd_events[fd])
					continue;
			}
			if ((ev_hdl->fd_events[fd]->flags & EV_WRITE) 
					&& FD_ISSET(fd,&xwfd)) {
				--retval;
				ev_hdl->fd_events[fd]->callback(
						ev_hdl->fd_events[fd],
						EV_WRITE);
				if (!ev_hdl->fd_events[fd])
					continue;
			}
			if ((ev_hdl->fd_events[fd]->flags & EV_EXCEPT) 
					&& FD_ISSET(fd,&xxfd)) {
				ev_hdl->fd_events[fd]->callback(
						ev_hdl->fd_events[fd],
						EV_EXCEPT);
				--retval;
			}
		}
	}
}

#if 0
void Log(char *msg,...)
{
	va_list va;
	va_start(va,msg);
	vprintf(msg,va);
	va_end(va);
}

void dump_state()
{
	struct wand_timer_t *tmp;
	tmp=timers;
	while(tmp) {
		printf("%p %p %p %d.%06d %.02f\n",(void*)tmp,
				(void*)tmp->prev,
				(void*)tmp->next,
				tmp->expire.tv_sec,tmp->expire.tv_usec,
				((float)tmp->expire.tv_sec+tmp->expire.tv_usec/1000000.0)-
					((float)wand_now.tv_sec+wand_now.tv_usec/1000000.0));
		tmp=tmp->next;
	}
}

#endif
