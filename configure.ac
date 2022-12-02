AC_PREREQ(2.60)
AC_INIT([netresolve], [0.0.1], [https://github.com/crossdistro/netresolve], [netresolve], [])
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
AC_ARG_WITH(ldns, AS_HELP_STRING([--with-ldns|--without-ldns], [Use ldns for DNS data operations (Default: yes)]), [use_ldns="$with_ldns"])/
AS_IF([test "$use_ldns" != no], [
	AC_CHECK_LIB([ldns], [ldns_wire2pkt], [use_ldns=yes], [use_ldns=no])
	if test "$with_ldns" = yes -a "$with_ldns" = no; then AC_MSG_ERROR([Bulding with ldns requested but not available.]); fi
])
AS_IF([test "$use_ldns" = yes], [
       AC_DEFINE(USE_LDNS, 1, [Use ldns library for DNS data operations (Default: yes).]))
       AC_SUBST(LDNS_LIBS, -lldns)
])

AC_ARG_WITH(c-ares, AS_HELP_STRING([--with-c-ares|--without-c-ares], [Build c-ares backend (Default: yes)]), [build_aresdns="$with_c_ares"])
AS_IF([test "$build_aresdns" != no], [
	PKG_CHECK_MODULES([ARES], [libcares], [build_aresdns=yes], [build_aresdns=no])
	if test "$use_ldns" = no; then build_aresdns=no; fi
	if test "$with_c_ares" = yes -a "$build_aresdns" = no; then AC_MSG_ERROR([Support for c-ares requested but not available.]); fi
])
AM_CONDITIONAL(BUILD_BACKEND_ARESDNS, [test $build_aresdns = yes])

AC_ARG_WITH(unbound, AS_HELP_STRING([--with-unbound|--without-unbound], [Build libunbound backend (Default: yes)]), [build_ubdns="$with_unbound"])
AS_IF([test "$build_ubdns" != no], [
	AC_CHECK_LIB([unbound], [ub_ctx_create], [build_ubdns=yes], [build_ubdns=no])
	if test "$use_ldns" = no; then build_ubdns=no; fi
	if test "$with_unbound" = yes -a "$build_ubdns" = no; then AC_MSG_ERROR([Support for libunbound requested but not available.]); fi
])
AM_CONDITIONAL(BUILD_BACKEND_UBDNS, [test "$build_ubdns" = yes])
AS_IF([test "$build_ubdns" = yes], AC_SUBST(UNBOUND_LIBS, -lunbound))

AC_ARG_WITH(avahi, AS_HELP_STRING([--with-avahi|--without-avahi], [Build libavahi backend (Default: yes)]), [build_avahi="$with_avahi"])
AS_IF([test "$build_avahi" != no], [
	PKG_CHECK_MODULES([AVAHI], [avahi-client], [build_avahi=yes], [build_avahi=no])
	if test "$with_avahi" = yes -a "$build_avahi" = no; then AC_MSG_ERROR([Support for libavahi requested but not available.]); fi
])
AM_CONDITIONAL(BUILD_BACKEND_AVAHI, [test $build_avahi = yes])

AC_ARG_WITH(asyncns, AS_HELP_STRING([--with-asyncns|--without-asyncns], [Build libasyncns based backend (Default: yes)]), [build_asyncns="$with_asyncns"])
AS_IF([test "$build_asyncns" != no], [
	PKG_CHECK_MODULES([ASYNCNS], [libasyncns], [build_asyncns=yes], [build_asyncns=no])
	if test "$with_asyncns" = yes -a "$build_asyncns" = no; then AC_MSG_ERROR([Support for libasyncns requested but not available.]); fi
])
AM_CONDITIONAL(BUILD_FRONTEND_ASYNCNS, [test $build_asyncns = yes])
AM_CONDITIONAL(BUILD_BACKEND_ASYNCNS, [test $build_asyncns = yes])

AC_ARG_ENABLE(tests, AS_HELP_STRING([--enable-tests|--disable-tests], [Build tests using additional dependencies (Default: no)]))
AS_IF([test "$enable_tests" = yes], [
	PKG_CHECK_MODULES([GLIB], [glib-2.0])
	AC_CHECK_LIB([event], [event_base_new], [EVENT_LIBS=-levent])
	AC_SUBST(EVENT_LIBS)
])
AM_CONDITIONAL(BUILD_ALL_TESTS, [test "$enable_tests" = yes])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
	tools/wrapresolve
])
AC_OUTPUT
AC_MSG_RESULT([
$PACKAGE $VERSION

Directories:

 * prefix: ${prefix}
 * sysconfdir: ${sysconfdir}
 * libdir: ${libdir}
 * includedir: ${includedir}

Compiler:

 * compiler: ${CC}
 * cflags: ${CFLAGS}
 * ldflags: ${LDFLAGS}

Optional frontends:

 * asyncns: $build_asyncns

Optional backends:

 * aresdns: $build_aresdns
 * ubdns: $build_ubdns
 * avahi: $build_avahi
 * asyncns: $build_asyncns

Other dependencies:

 * ldns: $use_ldns
])
