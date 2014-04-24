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

AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_HEADER([epoll.h])

PKG_CHECK_MODULES([ARES], [libcares])
AC_CHECK_LIB([unbound], [ub_ctx_create], [UNBOUND_LIBS=-lunbound])
AC_SUBST(UNBOUND_LIBS)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
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

