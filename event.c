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

#ifndef EVENT_DEBUG
#define EVENT_DEBUG 0
#endif

static fd_set rfd,wfd,xfd;
static struct wand_fdcb_t **events;
static struct wand_timer_t *timers;
static struct wand_timer_t *timers_tail;
static struct wand_signal_t **signals;
static int maxfd=-1;
static int maxsig=-1;
struct timeval wand_now;
bool wand_event_running;

int signal_pipe[2];
struct sigaction signal_event;
sigset_t active_sig;
struct wand_fdcb_t pipe_event;

struct sigaction default_sig;

void event_sig_hdl(int signum) {
	if (write(signal_pipe[1], &signum, 4) != 4) {
		printf("error writing signum to pipe\n");
	}
}

void pipe_read(struct wand_fdcb_t* evcb, enum wand_eventtype_t ev) {
	int signum = -1;
	struct wand_signal_t *signal;
	if (read(signal_pipe[0], &signum, 4) != 4) {
		printf("error reading signum from pipe\n");
		return;
	}
	
	if (signum > maxsig) {
		printf("signum %d > maxsig %d\n", signum, maxsig);
		return;
	}
	signal = signals[signum];
	while (signal != NULL) {
		signal->callback(signal);
		signal = signal->next;
	}
}

int wand_init_event()
{
	FD_ZERO(&rfd);
	FD_ZERO(&wfd);
	FD_ZERO(&xfd);
	events=NULL;
	timers=NULL;
	timers_tail=NULL;
	signals = NULL;
	maxsig = -1;
	maxfd=-1;
	wand_event_running=true;
	gettimeofday(&wand_now,NULL);

	sigemptyset(&active_sig);
	
	signal_event.sa_handler = event_sig_hdl;
	sigemptyset(&signal_event.sa_mask);
	signal_event.sa_flags = 0;
	
	default_sig.sa_handler = SIG_DFL;
	sigemptyset(&default_sig.sa_mask);
	default_sig.sa_flags = 0;
	
	if (pipe(signal_pipe) != 0) {
		printf("Error creating pipe\n");
		return -1;
	}
	
	/* Form evcb for signal_pipe[0] */
	pipe_event.fd = signal_pipe[0];
	pipe_event.flags = EV_READ;
	pipe_event.callback = pipe_read;
	pipe_event.data = 0;

	wand_add_event(&pipe_event);
	return 1;
}

struct timeval wand_calc_expire(int sec,int usec)
{
	struct timeval tmp;
	tmp.tv_sec=wand_now.tv_sec+sec;
	tmp.tv_usec=wand_now.tv_usec+usec;
	if (tmp.tv_usec>=1000000) {
		tmp.tv_sec+=1;
		tmp.tv_usec-=1000000;
	}
	return tmp;
}


void wand_add_signal(struct wand_signal_t *signal)
{
       	struct wand_signal_t *siglist;
	
	assert(signal->signum>0);

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
	}
	signal->prev = NULL;
	signal->next = siglist;
	signals[signal->signum] = signal;
}

void wand_del_signal(struct wand_signal_t *signal) 
{
	if (signal->prev)
		signal->prev->next = signal->next;
	else
		signals[signal->signum] = signal->next;

	if (signal->next)
		signal->next->prev = signal->prev;

	if (signals[signal->signum] == NULL) {
		sigdelset(&active_sig, signal->signum);
		if (sigaction(signal->signum, &default_sig, 0) < 0) {
			printf("Error removing sigaction\n");
		}
	}
}

#define TV_CMP(a,b) ((a).tv_sec == (b).tv_sec 		\
			? (a).tv_usec - (b).tv_usec 	\
			: (a).tv_sec - (b).tv_sec)

void wand_add_timer(struct wand_timer_t *timer)
{
	struct wand_timer_t *tmp = timers_tail;
	assert(timer->expire.tv_sec>=0);
	assert(timer->expire.tv_usec>=0);
	assert(timer->expire.tv_usec<1000000);
	if (timers==NULL) {
		timer->prev = timer->next = NULL;
		timers_tail=timers=timer;
		return;
	}
	assert(timer->prev == NULL);
	assert(timers_tail->next == NULL);
	
	/* Doubly linked lists are annoying! */
	/* FIXME: This code sucks ass */
	while(tmp->prev) {
		if (TV_CMP(tmp->expire, timer->expire) < 0) {
			/* insert */
			if (tmp->next)
				tmp->next->prev = timer;
			else 
				timers_tail = timer;
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
                        timers_tail = timer;
                timer->next = tmp->next;
                timer->prev = tmp;
                tmp->next = timer;
	} else {
		tmp->prev = timer;
		timer->next = tmp;
		timer->prev = NULL;
		timers = timer;
	}

}

void wand_del_timer(struct wand_timer_t *timer)
{
	assert(timer->prev!=(void*)0xdeadbeef);
	assert(timer->next!=(void*)0xdeadbeef);
	if (timer->prev)
		timer->prev->next=timer->next;
	else
		timers=timer->next;
	if (timer->next)
		timer->next->prev=timer->prev;
}

void wand_add_event(struct wand_fdcb_t *evcb)
{
	assert(evcb->fd>=0);
	assert(evcb->fd>=maxfd || events[evcb->fd]==NULL); /* can't add twice*/

	if (evcb->fd>maxfd) {
		events=realloc(events,sizeof(struct wand_fdcb_t)*(evcb->fd+1));
		/* FIXME: Deal with OOM */
		while(maxfd<evcb->fd) {
			events[++maxfd]=NULL;
		}
		maxfd=evcb->fd;
	}
	events[evcb->fd]=evcb;
	if (evcb->flags & EV_READ)   FD_SET(evcb->fd,&rfd);
	if (evcb->flags & EV_WRITE)  FD_SET(evcb->fd,&wfd);
	if (evcb->flags & EV_EXCEPT) FD_SET(evcb->fd,&xfd);
#if EVENT_DEBUG
	printf("New events for %d:",evcb->fd);
	if (evcb->flags & EV_READ) printf(" read");
	if (evcb->flags & EV_WRITE) printf(" write");
	if (evcb->flags & EV_EXCEPT) printf(" except");
	printf("\n");
#endif
}

void wand_del_event(struct wand_fdcb_t *evcb)
{
	assert(evcb->fd>=0);
	assert(evcb->fd<=maxfd && events[evcb->fd]!=NULL);
	events[evcb->fd]=NULL;
	if (evcb->flags & EV_READ)   FD_CLR(evcb->fd,&rfd);
	if (evcb->flags & EV_WRITE)  FD_CLR(evcb->fd,&wfd);
	if (evcb->flags & EV_EXCEPT) FD_CLR(evcb->fd,&xfd);
#if EVENT_DEBUG
	printf("del events for %d\n",evcb->fd);
#endif
}

void wand_event_run()
{
	struct wand_timer_t *tmp = 0;
	struct timeval delay;
	int fd;
	int retval;
	fd_set xrfd, xwfd, xxfd;
	
	while (wand_event_running) {
		gettimeofday(&wand_now,NULL);
		/* Expire old timers */
		while(timers && TV_CMP(wand_now,timers->expire)>0) {
			assert(timers->prev == NULL);
			tmp=timers;
			if (timers->next) {
				timers->next->prev = timers->prev;
			}
			timers=timers->next;
			if (timers==NULL)
				timers_tail=NULL;
			tmp->prev=(void*)0xdeadbeef;
			tmp->next=(void*)0xdeadbeef;
#if EVENT_DEBUG
			fprintf(stderr,"Timer expired\n");
#endif
			tmp->callback(tmp);
			if (!wand_event_running)
				return;
		}
		
		xrfd = rfd;
		xwfd = wfd;
		xxfd = xfd;
		sigprocmask(SIG_UNBLOCK, &active_sig, 0);
		if (timers) {
			delay.tv_sec = timers->expire.tv_sec - wand_now.tv_sec;
			delay.tv_usec = timers->expire.tv_usec - wand_now.tv_usec;
			if (delay.tv_usec<0) {
				delay.tv_usec += 1000000;
				--delay.tv_sec;
			}
		
			do {
				retval = select(maxfd+1,
						&xrfd,&xwfd,&xxfd,&delay);
				if (retval == -1 && errno != EINTR) {
					/* ERROR */
					printf("Error in select\n");
					return;
				}
			} while (retval == -1);
		} else {
			do {
				retval = select(maxfd+1,&xrfd,
						&xwfd,&xxfd,NULL);
				if (retval == -1 && errno != EINTR) {
					printf("Error in select\n");
					return;
				}
			} while (retval == -1);
		
		}
		sigprocmask(SIG_BLOCK, &active_sig, 0);
		/* TODO: check select's return */
		for(fd=0;fd<=maxfd;++fd) {
			/* Skip fd's we don't have events for */
			if (!events[fd])
				continue;
			assert(events[fd]->fd==fd);
			/* This code makes me feel dirty */
			if ((events[fd]->flags & EV_READ) 
					&& FD_ISSET(fd,&xrfd))
				events[fd]->callback(events[fd],EV_READ);
			if (events[fd] && (events[fd]->flags & EV_WRITE) 
					&& FD_ISSET(fd,&xwfd))
				events[fd]->callback(events[fd],EV_WRITE);
			if (events[fd] && (events[fd]->flags & EV_EXCEPT) 
					&& FD_ISSET(fd,&xxfd))
				events[fd]->callback(events[fd],EV_EXCEPT);
		}
	}
}

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

