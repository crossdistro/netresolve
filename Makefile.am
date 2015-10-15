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
	include/netresolve-nonblock.h \
	include/netresolve-event.h \
	include/netresolve-glib.h \
	include/netresolve-epoll.h \
	include/netresolve-select.h

lib_LTLIBRARIES = \
	libnetresolve.la \
	libnetresolve-libc.la \
	libnss_netresolve.la

libnetresolve_la_SOURCES = \
	include/netresolve-private.h \
	include/netresolve-backend.h \
	include/netresolve-cli.h \
	lib/request.c \
	lib/query.c \
	lib/backend.c \
	lib/context.c \
	lib/compat.c \
	lib/logging.c \
	lib/event.c \
	lib/service.c \
	lib/socket.c \
	lib/string.c \
	lib/epoll.c \
	lib/select.c
libnetresolve_la_LDFLAGS = \
	$(AM_LDFLAGS) $(LDNS_LIBS) \
	-export-symbols-regex '^netresolve_'

libnetresolve_libc_la_SOURCES = \
	include/netresolve-compat.h \
	compat/libc.c
libnetresolve_libc_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	--export-all-symbols
libnetresolve_libc_la_LIBADD = \
	libnetresolve.la

if BUILD_FRONTEND_ASYNCNS
lib_LTLIBRARIES += libnetresolve-asyncns.la

libnetresolve_asyncns_la_SOURCES = \
	compat/asyncns.c
libnetresolve_asyncns_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	--export-all-symbols
libnetresolve_asyncns_la_LIBADD = \
	libnetresolve.la
endif

libnss_netresolve_la_SOURCES = \
	compat/nsswitch.c
libnss_netresolve_la_LDFLAGS = $(AM_LDFLAGS) -version-info 2:0:0

lib_LTLIBRARIES += \
	libnetresolve-backend-unix.la \
	libnetresolve-backend-any.la \
	libnetresolve-backend-loopback.la \
	libnetresolve-backend-numerichost.la \
	libnetresolve-backend-hosts.la \
	libnetresolve-backend-hostname.la \
	libnetresolve-backend-libc.la \
	libnetresolve-backend-nss.la \
	libnetresolve-backend-exec.la

if BUILD_BACKEND_ASYNCNS
lib_LTLIBRARIES += libnetresolve-backend-asyncns.la
endif
if BUILD_BACKEND_ARESDNS
lib_LTLIBRARIES += libnetresolve-backend-aresdns.la
endif
if BUILD_BACKEND_UBDNS
lib_LTLIBRARIES += libnetresolve-backend-ubdns.la
endif
if BUILD_BACKEND_AVAHI
lib_LTLIBRARIES += libnetresolve-backend-avahi.la
endif

libnetresolve_backend_unix_la_SOURCES = backends/unix.c
libnetresolve_backend_any_la_SOURCES = backends/any.c
libnetresolve_backend_loopback_la_SOURCES = backends/loopback.c
libnetresolve_backend_numerichost_la_SOURCES = backends/numerichost.c
libnetresolve_backend_hosts_la_SOURCES = backends/hosts.c
libnetresolve_backend_hostname_la_SOURCES = backends/hostname.c
libnetresolve_backend_libc_la_SOURCES = backends/libc.c
libnetresolve_backend_libc_la_LDFLAGS = $(AM_LDFLAGS) -lresolv
libnetresolve_backend_nss_la_SOURCES = backends/nss.c
libnetresolve_backend_exec_la_SOURCES = backends/exec.c

if BUILD_BACKEND_ARESDNS
libnetresolve_backend_aresdns_la_SOURCES = backends/dns.c
libnetresolve_backend_aresdns_la_LDFLAGS = $(AM_LDFLAGS) $(ARES_LIBS)
libnetresolve_backend_aresdns_la_CPPFLAGS = $(AM_CPPFLAGS) $(ARES_CFLAGS) -DUSE_ARES=1
endif
if BUILD_BACKEND_UBDNS
libnetresolve_backend_ubdns_la_SOURCES = backends/dns.c
libnetresolve_backend_ubdns_la_LDFLAGS = $(AM_LDFLAGS) $(UNBOUND_LIBS)
libnetresolve_backend_ubdns_la_CPPFLAGS = $(AM_CPPFLAGS) -DUSE_UNBOUND=1
endif
if BUILD_BACKEND_AVAHI
libnetresolve_backend_avahi_la_SOURCES = backends/dns.c
libnetresolve_backend_avahi_la_LDFLAGS = $(AM_LDFLAGS) $(AVAHI_LIBS)
libnetresolve_backend_avahi_la_CPPFLAGS = $(AM_CPPFLAGS) -DUSE_AVAHI=1
endif
if BUILD_BACKEND_ASYNCNS
libnetresolve_backend_asyncns_la_SOURCES = backends/asyncns.c
libnetresolve_backend_asyncns_la_LDFLAGS = $(ASYNCNS_LIBS)
endif

bin_PROGRAMS = netresolve getaddrinfo getnameinfo gethostbyname gethostbyaddr res_query
bin_SCRIPTS = tools/wrapresolve

netresolve_SOURCES = tools/netresolve.c
netresolve_LDADD = libnetresolve.la
netresolve_LDFLAGS = $(AM_LDFLAGS) $(LDNS_LIBS)

getaddrinfo_SOURCES = tools/getaddrinfo.c tools/compat.c

getnameinfo_SOURCES = tools/getnameinfo.c tools/compat.c

gethostbyname_SOURCES = tools/gethostbyname.c tools/compat.c

gethostbyaddr_SOURCES = tools/gethostbyaddr.c tools/compat.c

res_query_SOURCES = tools/res_query.c tools/compat.c
res_query_LDFLAGS = $(AM_LDFLAGS) $(LDNS_LIBS) -lresolv

TESTS = \
	tests/test-netresolve.sh \
	test-sync \
	test-epoll \
	test-select \
	test-bind-connect \
	tests/test-compat.sh
EXTRA_DIST = \
	tools/compat.h \
	tests/common.h \
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
	test-epoll \
	test-select \
	test-bind-connect \
	test-getaddrinfo \
	test-gethostbyname \
	test-gethostbyname2 \
	test-gethostbyname_r \
	test-gethostbyname2_r
if BUILD_FRONTEND_ASYNCNS
noinst_PROGRAMS += \
	test-asyncns \
	test-asyncns-cancel
endif
if BUILD_ALL_TESTS
TESTS += \
	test-libevent \
	test-glib
noinst_PROGRAMS += \
	test-libevent \
	test-glib
endif

test_sync_SOURCES = tests/test-sync.c tests/common.c tests/common.h
test_sync_LDADD = libnetresolve.la

test_epoll_SOURCES = tests/test-async.c tests/test-async-epoll.c tests/common.c
test_epoll_LDADD = libnetresolve.la

test_select_SOURCES = tests/test-async.c tests/test-async-select.c tests/common.c
test_select_LDADD = libnetresolve.la

test_libevent_SOURCES = tests/test-libevent.c tests/common.c
test_libevent_LDADD = libnetresolve.la
test_libevent_LDFLAGS = $(AM_LDFLAGS) $(EVENT_LIBS)

test_glib_SOURCES = tests/test-glib.c tests/common.c
test_glib_LDADD = libnetresolve.la
test_glib_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)
test_glib_LDFLAGS = $(AM_LDFLAGS) $(GLIB_LIBS)

test_bind_connect_SOURCES = tests/test-bind-connect.c
test_bind_connect_LDADD = libnetresolve.la

test_getaddrinfo_SOURCES = tests/test-getaddrinfo.c

test_gethostbyname_SOURCES = tests/test-gethostbyname.c

test_gethostbyname2_SOURCES = tests/test-gethostbyname.c
test_gethostbyname2_CFLAGS = -DGETHOSTBYNAME2

test_gethostbyname_r_SOURCES = tests/test-gethostbyname.c
test_gethostbyname_r_CFLAGS = -DREENTRANT

test_gethostbyname2_r_SOURCES = tests/test-gethostbyname.c
test_gethostbyname2_r_CFLAGS = -DGETHOSTBYNAME2 -DREENTRANT

test_asyncns_SOURCES = tests/test-asyncns.c
test_asyncns_LDFLAGS = -lasyncns

test_asyncns_cancel_SOURCES = tests/test-asyncns-cancel.c
test_asyncns_cancel_LDFLAGS = -lasyncns
