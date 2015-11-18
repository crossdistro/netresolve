/* Copyright (c) 2013+ Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <netresolve-backend.h>
#include <ldns/ldns.h>

#if defined(USE_UNBOUND)

#include <unbound.h>
#define priv_dns priv_dns

static void ubdns_callback(void *arg, int status, struct ub_result* result);

#elif defined(USE_ARES)

#include <ares.h>
#define priv_dns priv_aresdns

static void aresdns_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen);

#elif defined(USE_AVAHI)

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#define priv_dns priv_avahi

static void record_callback (
	AvahiRecordBrowser *b,
	AvahiIfIndex interface,
	AvahiProtocol protocol,
	AvahiBrowserEvent event,
	const char *name,
	uint16_t class,
	uint16_t type,
	const void *rdata,
	size_t size,
	AvahiLookupResultFlags flags,
	void *userdata);
static void address_callback (
	AvahiAddressResolver *r,
	AvahiIfIndex interface,
	AvahiProtocol protocol,
	AvahiResolverEvent event,
	const AvahiAddress *a,
	const char *name,
	AvahiLookupResultFlags flags,
	void *userdata);

#endif

struct priv_srv {
	struct priv_dns *priv;
	struct priv_srv *previous, *next;
	int priority;
	int weight;
	uint16_t port;
	char *name;
};

struct priv_dns {
	netresolve_query_t query;
	int protocol;
	int family;
#if defined(USE_AVAHI)
	AvahiAddress address;
#else
	const uint8_t *address;
#endif
	int cls;
	int type;

	struct priv_srv srv;
	int pending;
	bool answered;
	bool failed;
	bool secure;
#if defined(USE_UNBOUND)
	struct ub_ctx* ctx;
	bool validate;
	netresolve_watch_t watch;
#elif defined(USE_ARES)
	ares_channel channel;
	fd_set rfds, wfds;
	int nfds;
	netresolve_watch_t *watches;
#elif defined(USE_AVAHI)
	struct AvahiClient *client;
	struct AvahiHostNameResolver *resolver;
	struct AvahiPoll poll_config;
	AvahiLookupFlags flags;
#endif
};

static void
lookup_dns(struct priv_srv *srv, const char *name, int type, int class)
{
	struct priv_dns *priv = srv->priv;

	debug("Looking up %s record for %s", ldns_rr_descript(type)->_name, name);

	priv->pending++;

#if defined(USE_UNBOUND)
	ub_resolve_async(priv->ctx, name, type, class, srv, ubdns_callback, NULL);
#elif defined(USE_ARES)
	ares_query(priv->channel, name, class, type, aresdns_callback, srv);
#elif defined(USE_AVAHI)
	if (!avahi_record_browser_new(
			priv->client,
			AVAHI_IF_UNSPEC,
			AVAHI_PROTO_UNSPEC,
			srv->name,
			class,
			type,
			priv->flags,
			record_callback,
			srv)) {
		error("Failed to create record browser: %s\n",
				avahi_strerror(avahi_client_errno(priv->client)));
		netresolve_backend_failed(priv->query);
	}
#endif
}

static const char *
protocol_to_string(int proto)
{
	switch (proto) {
	case IPPROTO_UDP:
		return "udp";
	case IPPROTO_TCP:
		return "tcp";
	case IPPROTO_SCTP:
		return "sctp";
	default:
		return "0";
	}
}

static void
lookup_srv(struct priv_dns *priv)
{
	char *name;

	if (asprintf(&name, "_%s._%s.%s",
			netresolve_backend_get_servname(priv->query),
			protocol_to_string(priv->protocol),
			netresolve_backend_get_nodename(priv->query)) != -1) {
		lookup_dns(&priv->srv, name, LDNS_RR_TYPE_SRV, LDNS_RR_CLASS_IN);
		free(name);
	} else {
		error("memory allocation failed");
		priv->failed = true;
	}
}

static void
lookup_host(struct priv_srv *srv)
{
	struct priv_dns *priv = srv->priv;

	if (priv->family == AF_INET || priv->family == AF_UNSPEC)
		lookup_dns(srv, srv->name, LDNS_RR_TYPE_A, LDNS_RR_CLASS_IN);
	if (priv->family == AF_INET6 || priv->family == AF_UNSPEC)
		lookup_dns(srv, srv->name, LDNS_RR_TYPE_AAAA, LDNS_RR_CLASS_IN);
}

static void
lookup_address(struct priv_dns *priv)
{
#if defined(USE_AVAHI)
	priv->pending++;

	if (!avahi_address_resolver_new(
			priv->client,
			AVAHI_IF_UNSPEC,
			priv->family == AVAHI_PROTO_UNSPEC,
			&priv->address,
			priv->flags,
			address_callback,
			priv)) {
		error("Failed to create record browser: %s\n",
				avahi_strerror(avahi_client_errno(priv->client)));
		netresolve_backend_failed(priv->query);
	}
#else
	char name[128];

	switch (priv->family) {
	case AF_INET:
		snprintf(name, sizeof name, "%d.%d.%d.%d.in-addr.arpa.",
				priv->address[3],
				priv->address[2],
				priv->address[1],
				priv->address[0]);
		break;
	case AF_INET6:
		snprintf(name, sizeof name, "%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.ip6.arpa",
				priv->address[0xf] & 0x0f, priv->address[0xf] >> 4,
				priv->address[0xe] & 0x0f, priv->address[0xe] >> 4,
				priv->address[0xd] & 0x0f, priv->address[0xd] >> 4,
				priv->address[0xc] & 0x0f, priv->address[0xc] >> 4,
				priv->address[0xb] & 0x0f, priv->address[0xb] >> 4,
				priv->address[0xa] & 0x0f, priv->address[0xa] >> 4,
				priv->address[0x9] & 0x0f, priv->address[0x9] >> 4,
				priv->address[0x8] & 0x0f, priv->address[0x8] >> 4,
				priv->address[0x7] & 0x0f, priv->address[0x7] >> 4,
				priv->address[0x6] & 0x0f, priv->address[0x6] >> 4,
				priv->address[0x5] & 0x0f, priv->address[0x5] >> 4,
				priv->address[0x4] & 0x0f, priv->address[0x4] >> 4,
				priv->address[0x3] & 0x0f, priv->address[0x3] >> 4,
				priv->address[0x2] & 0x0f, priv->address[0x2] >> 4,
				priv->address[0x1] & 0x0f, priv->address[0x1] >> 4,
				priv->address[0x0] & 0x0f, priv->address[0x0] >> 4);
		break;
	default:
		abort();
	}

	lookup_dns(&priv->srv, name, LDNS_RR_TYPE_PTR, LDNS_RR_CLASS_IN);
#endif
}

static void
set_name(struct priv_dns *priv, const char *name)
{
	char *s = strdup(name);
	char *last = s + strlen(s) - 1;

	if (*last == '.')
		*last = '\0';

	netresolve_backend_add_name_info(priv->query, s, NULL);

	free(s);
}

static void
apply_name(struct priv_dns *priv, const char *type, ldns_rr *rr)
{
	char *name = ldns_rdf2str(ldns_rr_rdf(rr, 0));

	debug("Found %s: %s", type, name);

	set_name(priv, name);

	free(name);
}

static void
apply_address(struct priv_srv *srv, const char *type, int family, ldns_rr *rr)
{
	debug("Found %s", type);

	netresolve_backend_add_path(srv->priv->query,
			family, ldns_rdf_data(ldns_rr_rdf(rr, 0)), 0,
			0, srv->priv->protocol, srv->port,
			srv->priority, srv->weight, ldns_rr_ttl(rr));

	srv->priv->answered = true;
}

static void
apply_srv(struct priv_dns *priv, ldns_rr *rr)
{
	struct priv_srv *srv = calloc(1, sizeof *srv);

	if (!srv) {
		error("Memory allocation failed.");
		netresolve_backend_failed(priv->query);
		return;
	}

	srv->priv = priv;
	srv->previous = priv->srv.previous;
	srv->next = &priv->srv;
	srv->previous->next = srv->next->previous = srv;

	srv->priority = ldns_rdf2native_int16(ldns_rr_rdf(rr, 0));
	srv->weight = ldns_rdf2native_int16(ldns_rr_rdf(rr, 1));
	srv->port = ldns_rdf2native_int16(ldns_rr_rdf(rr, 2));
	srv->name = ldns_rdf2str(ldns_rr_rdf(rr, 3));

	debug("Found SRV: %d %d %d %s", srv->priority, srv->weight, srv->port, srv->name);

	set_name(priv, srv->name);

	lookup_host(srv);
}

static void
apply_record(struct priv_srv *srv, ldns_rr *rr)
{
	struct priv_dns *priv = srv->priv;

	switch (ldns_rr_get_type(rr)) {
	case LDNS_RR_TYPE_CNAME:
		apply_name(priv, "CNAME", rr);
		break;
	case LDNS_RR_TYPE_PTR:
		apply_name(priv, "PTR", rr);
		priv->answered = true;
		break;
	case LDNS_RR_TYPE_A:
		apply_address(srv, "A", AF_INET, rr);
		break;
	case LDNS_RR_TYPE_AAAA:
		apply_address(srv, "AAAA", AF_INET6, rr);
		break;
	case LDNS_RR_TYPE_SRV:
		apply_srv(priv, rr);
		break;
	default:
		error("Unkown record type: %s", ldns_rr_descript(ldns_rr_get_type(rr))->_name);
		break;
	}
}

#if defined(USE_ARES) || defined(USE_UNBOUND)
static void
apply_answer(struct priv_srv *srv, const uint8_t *data, size_t length)
{
	struct priv_dns *priv = srv->priv;

	assert(data);
	assert(length);

	if (priv->type) {
		netresolve_backend_set_dns_answer(priv->query, data, length);
		priv->answered = true;
		return;
	}

	ldns_pkt *pkt;

	if (ldns_wire2pkt(&pkt, data, length)) {
		error("can't parse the DNS answer");
		priv->failed = true;
		return;
	}

	ldns_pkt_rcode rcode = ldns_pkt_get_rcode(pkt);
	ldns_rr_type type = ldns_pkt_question(pkt)->_rrs[0]->_rr_type;
	ldns_rr_list *answer = ldns_pkt_answer(pkt);

#if defined(USE_UNBOUND)
	if (!priv->validate)
#endif
	if (!ldns_pkt_ad(pkt))
		priv->secure = false;

	/* libunbound seems to sometimes return an empty result with
	 * rcode set to zero
	 */
	if (rcode == 0 && answer->_rr_count == 0) {
		debug("fixing up rcode because of zero rr_count after libunbound");
		rcode = LDNS_RCODE_NXDOMAIN;
	}

	switch (rcode) {
	case 0:
		break;
	case LDNS_RCODE_NXDOMAIN:
		debug("%s records not found (%d queries left)",
				ldns_rr_descript(type)->_name,
				priv->pending);
		if (type == LDNS_RR_TYPE_SRV)
			lookup_host(&priv->srv);
		else
			priv->failed = true;
		goto out;
	default:
		error("rcode: %d", rcode);
		priv->failed = true;
		goto out;
	}

	for (int i = 0; i < answer->_rr_count; i++) {
		ldns_rr *rr = answer->_rrs[i];

		apply_record(srv, rr);
	}

out:
	ldns_pkt_free(pkt);
}
#endif

static void
check(struct priv_dns *priv)
{
	assert(priv->pending >= 0);

	if (!priv->pending) {
		if (priv->answered) {
			if (priv->secure)
				netresolve_backend_set_secure(priv->query);
		}

#if defined(USE_AVAHI)
		if (priv->answered)
#else
		if (!priv->failed)
#endif
			netresolve_backend_finished(priv->query);
		else
			netresolve_backend_failed(priv->query);
	}
}

#if defined(USE_UNBOUND)

static void apply_answer(struct priv_srv *srv, const uint8_t *answer, size_t length);

static void
ubdns_callback(void *arg, int status, struct ub_result* result)
{
	struct priv_srv *srv = arg;
	struct priv_dns *priv = srv->priv;

	priv->pending--;

	if (status) {
		error("libunbound: %s", ub_strerror(status));
		priv->failed = true;
		return;
	}

	if (!result->secure)
		priv->secure = false;

	if (!result->bogus)
		apply_answer(srv, result->answer_packet, result->answer_len);
	else {
		error("libunbound: received bogus result");
		priv->secure = false;
		priv->failed = true;
	}

	ub_resolve_free(result);

	check(priv);
}

static void
dispatch(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data)
{
	struct priv_dns *priv = netresolve_backend_get_priv(query);

	ub_process(priv->ctx);
}

#elif defined(USE_ARES)

static void dispatch(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data);

static void
watch_file_descriptors(struct priv_dns *priv)
{
	assert(priv->nfds == 0);

	priv->nfds = ares_fds(priv->channel, &priv->rfds, &priv->wfds);
	priv->watches = realloc(priv->watches, priv->nfds * sizeof *priv->watches);

	for (int fd = 0; fd < priv->nfds; fd++) {
		if (FD_ISSET(fd, &priv->rfds))
			priv->watches[fd] = netresolve_watch_add(priv->query, fd, POLLIN, dispatch, NULL);
		if (FD_ISSET(fd, &priv->wfds))
			priv->watches[fd] = netresolve_watch_add(priv->query, fd, POLLOUT, dispatch, NULL);
	}
}

static void
unwatch_file_descriptors(struct priv_dns *priv)
{
	assert(priv->nfds != 0);

	for (int fd = 0; fd < priv->nfds; fd++)
		if (FD_ISSET(fd, &priv->rfds))
			netresolve_watch_remove(priv->query, priv->watches[fd], false);

	FD_ZERO(&priv->rfds);
	FD_ZERO(&priv->wfds);
	priv->nfds = 0;
}

static void
dispatch(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data)
{
	struct priv_dns *priv = netresolve_backend_get_priv(query);

	unwatch_file_descriptors(priv);
	ares_process_fd(priv->channel,
			events & POLLIN ? fd : ARES_SOCKET_BAD,
			events & POLLOUT ? fd : ARES_SOCKET_BAD);
	watch_file_descriptors(priv);

	check(priv);
}

static void apply_answer(struct priv_srv *srv, const uint8_t *answer, size_t length);

static void
aresdns_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen)
{
	struct priv_srv *srv = arg;
	struct priv_dns *priv = srv->priv;

	priv->pending--;

	switch (status) {
	case ARES_EDESTRUCTION:
		/* Cleaning up. */
		return;
	case ARES_SUCCESS:
	case ARES_ENOTFOUND:
		break;
	default:
		error("ares: %s", ares_strerror(status));
		priv->failed = true;
		return;
	}

	apply_answer(srv, abuf, alen);

	check(priv);
}

#elif defined(USE_AVAHI)

struct AvahiWatch {
	netresolve_query_t query;
	netresolve_watch_t watch;
	int fd;
	int events;
	AvahiWatchCallback callback;
	void *userdata;
};

struct AvahiTimeout {
	netresolve_query_t query;
	netresolve_timeout_t timeout;
	int fd;
	AvahiTimeoutCallback callback;
	void *userdata;
};

void
watch_callback(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data)
{
	AvahiWatch *w = data;

	w->callback(w, w->fd, events, w->userdata);
}

void
watch_update(AvahiWatch *w, AvahiWatchEvent event)
{
	if (w->watch) {
		netresolve_watch_remove(w->query, w->watch, false);
		w->watch = NULL;
	}

	/* FIXME: The event should be properly translated but so far it worked. */
	if (event)
		w->watch = netresolve_watch_add(w->query, w->fd, event, watch_callback, w);
}

AvahiWatch *
watch_new(const AvahiPoll *api, int fd, AvahiWatchEvent event, AvahiWatchCallback callback, void *userdata)
{
	struct priv_avahi *priv = api->userdata;
	AvahiWatch *w = calloc(1, sizeof *w);

	w->query = priv->query;
	w->fd = fd;
	w->callback = callback;
	w->userdata = userdata;

	watch_update(w, event);

	return w;
}

static void
watch_free(AvahiWatch *w)
{
	watch_update(w, 0);

	memset(w, 0, sizeof *w);
	free(w);
}

void
timeout_callback(netresolve_query_t query, netresolve_timeout_t timeout, void *data)
{
	AvahiTimeout *t = data;

	t->callback(t, t->userdata);
}

static void
timeout_update(AvahiTimeout *t, const struct timeval *tv)
{
	if (t->timeout) {
		netresolve_timeout_remove(t->query, t->timeout);
		t->timeout = NULL;
	}

	if (tv) {
		long int sec = tv->tv_sec;
		long int nsec = tv->tv_usec * 1000;

		t->timeout = netresolve_timeout_add(t->query, sec, nsec, timeout_callback, t);
	}
}

AvahiTimeout *
timeout_new(const AvahiPoll *api, const struct timeval *tv, AvahiTimeoutCallback callback, void *userdata)
{
	struct priv_avahi *priv = api->userdata;
	AvahiTimeout *t = calloc(1, sizeof *t);

	t->query = priv->query;
	t->fd = -1;
	t->callback = callback;
	t->userdata = userdata;

	timeout_update(t, tv);

	return t;
}

void
timeout_free(AvahiTimeout *t)
{
	timeout_update(t, NULL);

	memset(t, 0, sizeof *t);
	free(t);
}

static void
client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
	struct priv_avahi *priv = userdata;

	if (state == AVAHI_CLIENT_FAILURE) {
		error("Avahi connection failure: %s", avahi_strerror(avahi_client_errno(c)));
		netresolve_backend_failed(priv->query);
	}
}

static void
record_callback (
	AvahiRecordBrowser *b,
	AvahiIfIndex interface,
	AvahiProtocol protocol,
	AvahiBrowserEvent event,
	const char *name,
	uint16_t class,
	uint16_t type,
	const void *rdata,
	size_t size,
	AvahiLookupResultFlags flags,
	void *userdata)
{
	struct priv_srv *srv = userdata;
	struct priv_avahi *priv = srv->priv;

	switch (event) {
		case AVAHI_BROWSER_NEW:
			{
				ldns_rr *rr = ldns_rr_new_frm_type(type);
				ldns_rdf *rdf = NULL;

				switch (type) {
				case LDNS_RR_TYPE_A:
					rdf = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_A, size, rdata);
					break;
				case LDNS_RR_TYPE_AAAA:
					rdf = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_AAAA, size, rdata);
					break;
				default:
					error("Unexpected RR type: %i", type);
					return;
				}
				if (rdf)
					ldns_rr_set_rdf(rr, rdf, 0);

				apply_record(srv, rr);

				ldns_rr_free(rr);
			}
			break;
		case AVAHI_BROWSER_ALL_FOR_NOW:
			debug("avahi: all for now");
			priv->pending--;
			assert(!rdata);
			break;
		case AVAHI_BROWSER_CACHE_EXHAUSTED:
			debug("avahi: cache exhausted");
			assert(!rdata);
			break;
		case AVAHI_BROWSER_FAILURE:
			error("Avahi resolver failed.");
			netresolve_backend_failed(priv->query);
			break;
		default:
			error("Unknown Avahi event: %d", event);
			netresolve_backend_failed(priv->query);
			return;
	}

	check(priv);
}

static void
address_callback (
	AvahiAddressResolver *r,
	AvahiIfIndex interface,
	AvahiProtocol protocol,
	AvahiResolverEvent event,
	const AvahiAddress *a,
	const char *name,
	AvahiLookupResultFlags flags,
	void *userdata)
{
	struct priv_avahi *priv = userdata;

	priv->pending--;

	switch (event) {
		case AVAHI_RESOLVER_FOUND:
			netresolve_backend_add_name_info(priv->query, name, NULL);
			break;
		case AVAHI_RESOLVER_FAILURE:
			error("Avahi resolver failed.");
			netresolve_backend_failed(priv->query);
			break;
		default:
			error("Unknown Avahi event: %d", event);
			netresolve_backend_failed(priv->query);
			return;
	}

	check(priv);
}

#endif

static void
cleanup(void *data)
{
	struct priv_dns *priv = data;

	while (priv->srv.next != &priv->srv) {
		struct priv_srv *srv = priv->srv.next;

		priv->srv.next = srv->next;
		srv->next->previous = &priv->srv;

		free(srv->name);
		free(srv);
	}

#if defined(USE_UNBOUND)
	if (priv->watch)
		netresolve_watch_remove(priv->query, priv->watch, false);
	if (priv->ctx);
		ub_ctx_delete(priv->ctx);
#elif defined(USE_ARES)
		if (priv->nfds)
		unwatch_file_descriptors(priv);
	free(priv->watches);
	ares_destroy(priv->channel);
#elif defined(USE_AVAHI)
	if (priv->client)
		avahi_client_free(priv->client);
#endif
}

static struct priv_dns *
setup(netresolve_query_t query, char **settings)
{
	struct priv_dns *priv = netresolve_backend_new_priv(query, sizeof *priv, cleanup);
	int status;
#if defined(USE_UNBOUND)
	const char *server = NULL;
#elif defined(USE_UNBOUND)

#endif

	if (!priv)
		return NULL;;

	priv->srv.priv = priv;
	priv->srv.previous = priv->srv.next = &priv->srv;

	for (; *settings; settings++) {
		if (!strcmp(*settings, "trust"))
			priv->secure = true;
#if defined(USE_UNBOUND)
		else if (!strcmp(*settings, "validate"))
			priv->validate = priv->secure = true;
		else if (!strncmp(*settings, "server=", 7))
			server = *settings + 7;
#endif
	}

	const char *name = netresolve_backend_get_nodename(query);
	priv->srv.name = name ? strdup(name) : NULL;
	priv->family = netresolve_backend_get_family(query);

	priv->query = query;

#if defined(USE_UNBOUND)
	priv->ctx = ub_ctx_create();
	if(!priv->ctx)
		return NULL;;

	if (server) {
		if ((status = ub_ctx_set_fwd(priv->ctx, server)) != 0) {
			error("libunbound: %s", ub_strerror(status));
			return NULL;
		}
	} else {
		if ((status = ub_ctx_resolvconf(priv->ctx, NULL)) != 0) {
			error("libunbound: %s", ub_strerror(status));
			return NULL;;
		}
	}
	if (priv->validate && (status = ub_ctx_add_ta_file(priv->ctx, "/etc/dnssec/root-anchors.txt"))) {
		error("libunbound: %s", ub_strerror(status));
		return NULL;
	}
	priv->watch = netresolve_watch_add(query, ub_fd(priv->ctx), POLLIN, dispatch, NULL);
#elif defined(USE_ARES)
	/* ares doesn't seem to accept const options */
	static struct ares_options options = {
		.flags = ARES_FLAG_NOSEARCH | ARES_FLAG_NOALIASES,
		.lookups = "b"
	};

	status = ares_library_init(ARES_LIB_INIT_ALL);
	if (status != ARES_SUCCESS) {
		error("ares library: %s", ares_strerror(status));
		return NULL;
	}
	status = ares_init_options(&priv->channel, &options,
			ARES_OPT_FLAGS | ARES_OPT_LOOKUPS);
	if (status != ARES_SUCCESS) {
		error("ares channel: %s", ares_strerror(status));
		return NULL;
	}
#elif defined(USE_AVAHI)
	priv->poll_config.userdata = priv;
	priv->poll_config.watch_new = watch_new;
	priv->poll_config.watch_update = watch_update;
	//priv->poll_config.watch_get_events = watch_get_events;
	priv->poll_config.watch_free = watch_free;
	priv->poll_config.timeout_new = timeout_new;
	priv->poll_config.timeout_update = timeout_update;
	priv->poll_config.timeout_free = timeout_free;

	priv->client = avahi_client_new(&priv->poll_config, 0, client_callback, priv, &status);
	if (!priv->client) {
		error("Avahi client failure: %s", avahi_strerror(status));
		return NULL;
	}
#endif

	return priv;
}

void
query_forward(netresolve_query_t query, char **settings)
{
	struct priv_dns *priv;

	if (!(priv = setup(query, settings)) || !priv->srv.name) {
		netresolve_backend_failed(query);
		return;
	}

	if (netresolve_backend_get_dns_srv_lookup(query)) {
		priv->protocol = netresolve_backend_get_protocol(priv->query);
		lookup_srv(priv);
	} else
		lookup_host(&priv->srv);

#if defined(USE_ARES)
	watch_file_descriptors(priv);
#endif
}

void
query_reverse(netresolve_query_t query, char **settings)
{
	struct priv_dns *priv;
	const uint8_t *address = netresolve_backend_get_address(query);

	if (!(priv = setup(query, settings)) || !address) {
		netresolve_backend_failed(query);
		return;
	}

#if defined(USE_AVAHI)

	switch (priv->family) {
	case AF_INET:
		priv->address.proto = AVAHI_PROTO_INET;
		memcpy(&priv->address.data, netresolve_backend_get_address(query), sizeof priv->address.data.ipv4);
		break;
	case AF_INET6:
		priv->address.proto = AVAHI_PROTO_INET6;
		memcpy(&priv->address.data, netresolve_backend_get_address(query), sizeof priv->address.data.ipv6);
		break;
	default:
		break;
	}
#else
	priv->address = address;
#endif

	lookup_address(priv);

#if defined(USE_ARES)
	watch_file_descriptors(priv);
#endif
}

void
query_dns(netresolve_query_t query, char **settings)
{
	struct priv_dns *priv;

	if (!(priv = setup(query, settings)) || !priv->srv.name) {
		netresolve_backend_failed(query);
		return;
	}

	netresolve_backend_get_dns_query(query, &priv->cls, &priv->type);
	lookup_dns(&priv->srv, priv->srv.name, priv->type, priv->cls);

#if defined(USE_ARES)
	watch_file_descriptors(priv);
#endif
}
