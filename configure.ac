AC_INIT(libwandevent, 3.0.2, contact@wand.net.nz)
AC_CONFIG_SRCDIR([event.c])
AM_CONFIG_HEADER([config.h])

AM_INIT_AUTOMAKE(libwandevent, 3.0.2)

AC_PROG_YACC
AC_PROG_CC
AM_PROG_LEX
AC_PROG_INSTALL
AC_PROG_LIBTOOL

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([inttypes.h stddef.h stdlib.h string.h syslog.h unistd.h])
AC_CHECK_LIB([rt], [clock_gettime])
# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

if test "$GCC" = "yes"; then
	CFLAGS="$CFLAGS -Wall -W -Wmissing-prototypes"
fi

# Checks for library functions.
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([memset strdup])

AC_ARG_WITH(epoll, AS_HELP_STRING(--without-epoll, prefer select over epoll),
[
	if test "$withval" = no 
	then
		want_epoll=no
	else
		want_epoll=yes
	fi
],[
	want_epoll=yes
])

if test "x$want_epoll" != "xno"; then
	AC_CHECK_HEADERS([sys/epoll.h])
fi

AC_CONFIG_FILES([Makefile])

AM_CONDITIONAL([BUILD_EPOLL],[test "$ac_cv_header_sys_epoll_h" = yes])
AC_OUTPUT

