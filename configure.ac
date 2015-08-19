AC_PREREQ(2.60)
AC_INIT([netresolve], [0.0.1], [psimerda@redhat.com], [netresolve], [])
AC_CONFIG_SRCDIR([tools/netresolve.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([check-news foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz subdir-objects])
AC_USE_SYSTEM_EXTENSIONS
LT_INIT([disable-static pic-only dlopen])
AC_PROG_CC_STDC
AC_CHECK_FUNC(__secure_getenv,
	AC_DEFINE(secure_getenv, __secure_getenv, [A prefixed version of secure_getenv found.]))
AC_SYS_LARGEFILE
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])
AC_PROG_SED
AC_PROG_INSTALL

# mandatory dependencies
AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_HEADER([epoll.h])

# optional dependencies
AC_ARG_WITH(c-ares, AS_HELP_STRING([--with-c-ares|--without-c-ares], [Build c-ares backend (Default: yes)]), [build_aresdns="$with_c_ares"])
if test "$build_aresdns" != no; then
	PKG_CHECK_MODULES([ARES], [libcares], [build_aresdns=yes], [build_aresdns=no])
	if test "$with_c_ares" = yes -a "$build_aresdns" = no; then AC_MSG_ERROR([Support for c-ares requested but not available.]); fi
fi
AM_CONDITIONAL(BUILD_BACKEND_ARESDNS, [test $build_aresdns])

AC_ARG_WITH(unbound, AS_HELP_STRING([--with-unbound|--without-unbound], [Build libunbound backend (Default: yes)]), [build_ubdns="$with_unbound"])
if test "$build_ubdns" != no; then
	AC_CHECK_LIB([unbound], [ub_ctx_create], [build_ubdns=yes], [build_ubdns=no])
	if test "$with_unbound" = yes -a "$build_ubdns" = no; then AC_MSG_ERROR([Support for libunbound requested but not available.]); fi
fi
AM_CONDITIONAL(BUILD_BACKEND_UBDNS, [test $build_ubdns])

AC_ARG_ENABLE(tests, AS_HELP_STRING([--enable-tests|--disable-tests], [Build tests using additional dependencies (Default: no)]))
AS_IF([test "$enable_tests" = "yes"],
	PKG_CHECK_MODULES([GLIB], [glib-2.0])
	AC_CHECK_LIB([event], [event_base_new], [EVENT_LIBS=-levent])
	AC_SUBST(EVENT_LIBS))
AM_CONDITIONAL(BUILD_ALL_TESTS, [test "$enable_tests" = "yes"])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
	tools/wrapresolve
])
AC_OUTPUT
AC_MSG_RESULT([
$PACKAGE $VERSION

prefix: ${prefix}
sysconfdir: ${sysconfdir}
libdir: ${libdir}
includedir: ${includedir}

compiler: ${CC}
cflags: ${CFLAGS}
ldflags: ${LDFLAGS}
])

echo "Optional backends:"
echo " * aresdns: $build_aresdns"
echo " * ubdns: $build_ubdns"
echo
