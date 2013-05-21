lib_LTLIBRARIES = libwandevent.la
include_HEADERS = libwandevent.h

if BUILD_EPOLL
HELPERSOURCE=epollhelper.c epollhelper.h
else
HELPERSOURCE=selecthelper.c selecthelper.h
endif

libwandevent_la_SOURCES = event.c libwandevent.h $(HELPERSOURCE)
libwandevent_la_LDFLAGS = -version-info 3:0:0


