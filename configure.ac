# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT(simvacation, m4_esyscmd([bash version.sh]), simta@umich.edu)
AM_INIT_AUTOMAKE([foreign subdir-objects no-dist-gzip dist-zstd 1.16.2])
AC_CONFIG_SRCDIR([simunvacation.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Set up variables
AC_ARG_WITH(vacationdb, AC_HELP_STRING([--with-vacationdb=DB], [default: /var/lib/simvacation]), [], with_vacationdb="/var/lib/simvacation")
AC_DEFINE_UNQUOTED(VDBDIR, ["$with_vacationdb"], [vacation db location])

AC_ARG_WITH(syslog, AC_HELP_STRING([--with-syslog=FACILITY], [default: LOG_MAIL]), [], with_syslog="LOG_MAIL")
AC_DEFINE_UNQUOTED(LOG_VACATION, [$with_syslog], [syslog facility])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99

AC_USE_SYSTEM_EXTENSIONS

# Checks for libraries.
PKG_CHECK_MODULES([LIBUCL], [libucl])

AC_ARG_WITH([redis], AC_HELP_STRING([--with-redis], [Build with Redis support]))
AS_IF([test x$with_redis != 'xno'],
    [PKG_CHECK_MODULES([URCL], [urcl >= 1.1], [AC_DEFINE([HAVE_URCL], [1], [Define to 1 if URCL is found])])])

AX_WITH_LIBRARY([lmdb], [LMDB], [lmdb.h], [lmdb])
AX_WITH_LIBRARY([ldap], [LDAP], [ldap.h], [ldap], [-llber])

AC_ARG_WITH([cmocka], AC_HELP_STRING([--with-cmocka], [Build unit tests]))
AS_IF(
    [test "x$with_cmocka" = "xyes"],
    [PKG_CHECK_MODULES([CMOCKA], [cmocka], [AC_DEFINE([HAVE_CMOCKA], [1], [Define to 1 if CMOCKA is available])])])

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h stdlib.h string.h strings.h sys/param.h sys/time.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_FORK
AC_CHECK_FUNCS([dup2 memset strcasecmp strdup strerror strncasecmp strrchr])

AC_SUBST(EXTRALIBS, [])

AM_CONDITIONAL(BUILD_CMOCKA, [test x"$with_cmocka" = xyes])
AM_CONDITIONAL(BUILD_LDAP, [test x"$ax_cv_have_LDAP" = 'xyes'])
AM_CONDITIONAL(BUILD_LMDB, [test x"$ax_cv_have_LMDB" = xyes])
AM_CONDITIONAL(BUILD_REDIS, [test x"$with_redis" != 'xno'])

AC_CONFIG_FILES([Makefile packaging/rpm/simvacation.spec test/Makefile])
AC_OUTPUT
