ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-I${top_srcdir}/include \
	-I${top_srcdir}/lib \
	-I${top_srcdir}/compat

AM_CFLAGS = -Wall -Werror
AM_LDFLAGS =

SED_PROCESS = \
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(SED) \
	-e 's,@VERSION\@,$(VERSION),g' \
	-e 's,@prefix\@,$(prefix),g' \
	-e 's,@exec_prefix\@,$(exec_prefix),g' \
	-e 's,@libdir\@,$(libdir),g' \
	-e 's,@includedir\@,$(includedir),g' \
	< $< > $@ || rm $@

%.pc: %.pc.in Makefile
	$(SED_PROCESS)

include_HEADERS = \
	include/netresolve.h \
	include/netresolve-utils.h

lib_LTLIBRARIES = \
	libnetresolve.la \
	libnetresolve-libc.la \
	libnss_netresolve.la

libnetresolve_la_SOURCES = \
	lib/netresolve-private.h \
	lib/netresolve-backend.h \
	lib/netresolve-string.h \
	lib/netresolve-cli.h \
	lib/channel.c \
	lib/input.c \
	lib/output.c \
	lib/logging.c \
	lib/backend.c \
	lib/services.c \
	lib/socket.c \
	lib/utils.c \
	lib/string.c \
	lib/cli.c
libnetresolve_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-export-symbols-regex '^netresolve_'

libnetresolve_libc_la_SOURCES = \
	compat/netresolve-compat.h \
	compat/libc.c \
	compat/compat.c
libnetresolve_libc_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	--export-all-symbols
libnetresolve_libc_la_LIBADD = \
	libnetresolve.la

libnss_netresolve_la_SOURCES = \
	compat/nsswitch.c \
	compat/compat.c

lib_LTLIBRARIES += \
	libnetresolve-backend-unix.la \
	libnetresolve-backend-any.la \
	libnetresolve-backend-loopback.la \
	libnetresolve-backend-numerichost.la \
	libnetresolve-backend-hosts.la \
	libnetresolve-backend-aresdns.la \
	libnetresolve-backend-ubdns.la \
	libnetresolve-backend-getaddrinfo.la \
	libnetresolve-backend-nss.la \
	libnetresolve-backend-exec.la
libnetresolve_backend_unix_la_SOURCES = backends/unix.c
libnetresolve_backend_any_la_SOURCES = backends/any.c
libnetresolve_backend_loopback_la_SOURCES = backends/loopback.c
libnetresolve_backend_numerichost_la_SOURCES = backends/numerichost.c
libnetresolve_backend_hosts_la_SOURCES = backends/hosts.c
libnetresolve_backend_aresdns_la_SOURCES = backends/aresdns.c
libnetresolve_backend_aresdns_la_CPPFLAGS = $(AM_CPPFLAGS) $(ARES_CFLAGS)
libnetresolve_backend_aresdns_la_LDFLAGS = $(AM_LDFLAGS) $(ARES_LIBS)
libnetresolve_backend_ubdns_la_SOURCES = backends/ubdns.c
libnetresolve_backend_ubdns_la_LDFLAGS = $(AM_LDFLAGS) $(UNBOUND_LIBS) -lldns
libnetresolve_backend_getaddrinfo_la_SOURCES = backends/getaddrinfo.c
libnetresolve_backend_nss_la_SOURCES = backends/nss.c
libnetresolve_backend_exec_la_SOURCES = backends/exec.c

bin_PROGRAMS = netresolve netconnect

netresolve_SOURCES = tools/netresolve.c
netresolve_LDADD = libnetresolve.la

netconnect_SOURCES = tools/netconnect.c
netconnect_LDADD = libnetresolve.la

TESTS = \
	tests/test-netresolve.sh \
	test-sync \
	test-async \
	test-libevent \
	test-bind-connect \
	test-getaddrinfo \
	test-gethostbyname
EXTRA_DIST = \
	tests/test-netresolve.sh \
	tests/data/any \
	tests/data/localhost \
	tests/data/localhost \
	tests/data/localhost4 \
	tests/data/localhost6 \
	tests/data/numeric4 \
	tests/data/numeric4lo \
	tests/data/numeric4lo \
	tests/data/numeric4nines \
	tests/data/empty \
	tests/data/numeric6 \
	tests/data/numeric6lo \
	tests/data/numeric6nines \
	tests/data/empty \
	tests/data/dns \
	tests/data/services \
	tests/data/unix \
	tests/data/unix-stream \
	tests/data/unix-dgram \
	tests/data/empty
noinst_PROGRAMS = \
	test-sync \
	test-async \
	test-libevent \
	test-bind-connect \
	test-getaddrinfo \
	test-gethostbyname

test_sync_SOURCES = tests/test-sync.c tests/common.c tests/common.h
test_sync_LDADD = libnetresolve.la

test_async_SOURCES = tests/test-async.c tests/common.c tests/common.h
test_async_LDADD = libnetresolve.la

test_libevent_SOURCES = tests/test-libevent.c tests/common.c tests/common.h
test_libevent_LDADD = libnetresolve.la
test_libevent_LDFLAGS = $(AM_LDFLAGS) $(EVENT_LIBS)

test_bind_connect_SOURCES = tests/test-bind-connect.c
test_bind_connect_LDADD = libnetresolve.la

test_getaddrinfo_SOURCES = tests/test-getaddrinfo.c
test_getaddrinfo_LDADD = libnetresolve-libc.la

test_gethostbyname_SOURCES = tests/test-gethostbyname.c
test_gethostbyname_LDADD = libnetresolve-libc.la
