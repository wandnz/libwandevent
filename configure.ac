AC_INIT(libwandevent, 2.0.0, contact@wand.net.nz)
AC_CONFIG_SRCDIR([event.c])
AM_CONFIG_HEADER([config.h])

AM_INIT_AUTOMAKE(libwandevent, 2.0.0)

AC_PROG_YACC
AC_PROG_CC
AM_PROG_LEX
AC_PROG_INSTALL
AC_PROG_LIBTOOL

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([inttypes.h stddef.h stdlib.h string.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([memset strdup])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

