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
	libnetresolve-asyncns.la \
	libnss_netresolve.la

libnetresolve_la_SOURCES = \
	include/netresolve-private.h \
	include/netresolve-backend.h \
	include/netresolve-cli.h \
	lib/backend.c \
	lib/channel.c \
	lib/compat.c \
	lib/logging.c \
	lib/query.c \
	lib/service.c \
	lib/socket.c \
	lib/string.c \
	lib/utils.c
libnetresolve_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-export-symbols-regex '^netresolve_'

libnetresolve_libc_la_SOURCES = \
	include/netresolve-compat.h \
	compat/libc.c
libnetresolve_libc_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	--export-all-symbols
libnetresolve_libc_la_LIBADD = \
	libnetresolve.la

libnetresolve_asyncns_la_SOURCES = \
	compat/asyncns.c
libnetresolve_asyncns_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	--export-all-symbols
libnetresolve_asyncns_la_LIBADD = \
	libnetresolve.la

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
	libnetresolve-backend-aresdns.la \
	libnetresolve-backend-ubdns.la \
	libnetresolve-backend-getaddrinfo.la \
	libnetresolve-backend-asyncns.la \
	libnetresolve-backend-nss.la \
	libnetresolve-backend-exec.la
libnetresolve_backend_unix_la_SOURCES = backends/unix.c
libnetresolve_backend_any_la_SOURCES = backends/any.c
libnetresolve_backend_loopback_la_SOURCES = backends/loopback.c
libnetresolve_backend_numerichost_la_SOURCES = backends/numerichost.c
libnetresolve_backend_hosts_la_SOURCES = backends/hosts.c
libnetresolve_backend_hostname_la_SOURCES = backends/hostname.c
libnetresolve_backend_aresdns_la_SOURCES = backends/aresdns.c
libnetresolve_backend_aresdns_la_CPPFLAGS = $(AM_CPPFLAGS) $(ARES_CFLAGS)
libnetresolve_backend_aresdns_la_LDFLAGS = $(AM_LDFLAGS) $(ARES_LIBS)
libnetresolve_backend_ubdns_la_SOURCES = backends/ubdns.c
libnetresolve_backend_ubdns_la_LDFLAGS = $(AM_LDFLAGS) $(UNBOUND_LIBS) -lldns
libnetresolve_backend_getaddrinfo_la_SOURCES = backends/getaddrinfo.c
libnetresolve_backend_asyncns_la_SOURCES = backends/asyncns.c
libnetresolve_backend_asyncns_la_LDFLAGS = -lasyncns
libnetresolve_backend_nss_la_SOURCES = backends/nss.c
libnetresolve_backend_exec_la_SOURCES = backends/exec.c

bin_PROGRAMS = netresolve netconnect getaddrinfo getnameinfo gethostbyname gethostbyaddr res_query
bin_SCRIPTS = tools/wrapresolve

netresolve_SOURCES = tools/netresolve.c tools/common.c
netresolve_LDADD = libnetresolve.la
netresolve_LDFLAGS = $(AM_LDFLAGS) -lldns

netconnect_SOURCES = tools/netconnect.c tools/common.c
netconnect_LDADD = libnetresolve.la
netconnect_LDFLAGS = $(AM_LDFLAGS) -lldns

getaddrinfo_SOURCES = tools/getaddrinfo.c tools/compat.c

getnameinfo_SOURCES = tools/getnameinfo.c tools/compat.c

gethostbyname_SOURCES = tools/gethostbyname.c tools/compat.c

gethostbyaddr_SOURCES = tools/gethostbyaddr.c tools/compat.c

res_query_SOURCES = tools/res_query.c tools/compat.c
res_query_LDFLAGS = $(AM_LDFLAGS) -lresolv

TESTS = \
	tests/test-netresolve.sh \
	test-sync \
	test-async \
	test-libevent \
	test-glib \
	test-bind-connect \
	tests/test-compat.sh
EXTRA_DIST = \
	tools/compat.h \
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
	test-glib \
	test-bind-connect \
	test-getaddrinfo \
	test-gethostbyname \
	test-gethostbyname2 \
	test-gethostbyname_r \
	test-gethostbyname2_r \
	test-asyncns \
	test-asyncns-cancel

test_sync_SOURCES = tests/test-sync.c tests/common.c tests/common.h
test_sync_LDADD = libnetresolve.la

test_async_SOURCES = tests/test-async.c tests/common.c tests/common.h
test_async_LDADD = libnetresolve.la

test_libevent_SOURCES = tests/test-libevent.c tests/common.c tests/common.h
test_libevent_LDADD = libnetresolve.la
test_libevent_LDFLAGS = $(AM_LDFLAGS) $(EVENT_LIBS)

test_glib_SOURCES = tests/test-glib.c tests/common.c tests/common.h
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
