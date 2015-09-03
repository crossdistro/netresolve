/* Copyright (c) 2013 Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
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
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>

#include <stdlib.h>
#include <assert.h>

struct priv_avahi {
	netresolve_query_t query;
	struct AvahiClient *client;
	struct AvahiHostNameResolver *resolver;
	struct AvahiPoll poll_config;
	char *name;
	int family;
	int pending;
};

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

		if (sec == 0 && nsec == 0)
			nsec = 1;
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
host_name_resolver_callback(
	AvahiHostNameResolver *r,
	AVAHI_GCC_UNUSED AvahiIfIndex interface,
	AVAHI_GCC_UNUSED AvahiProtocol protocol,
	AvahiResolverEvent event,
	const char *name,
	const AvahiAddress *a,
	AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
	AVAHI_GCC_UNUSED void *userdata)
{
	struct priv_avahi *priv = userdata;

	priv->pending--;

	switch (event) {
		case AVAHI_RESOLVER_FOUND:
			netresolve_backend_add_path(priv->query,
					protocol ? AF_INET6 : AF_INET,
					&a->data,
					protocol && IN6_IS_ADDR_LINKLOCAL(&a->data) ? interface : 0,
					0, 0, 0,
					0, 0, 0);
			break;
		case AVAHI_RESOLVER_FAILURE:
			netresolve_backend_failed(priv->query);
			break;
		default:
			abort();
	}
}

static void
lookup_host(struct priv_avahi *priv, int protocol)
{
	if (!avahi_host_name_resolver_new(
				priv->client,
				AVAHI_IF_UNSPEC,
				AVAHI_PROTO_UNSPEC,
				priv->name,
				protocol,
				0,
				host_name_resolver_callback,
				priv)) {
		error("Failed to create host name resolver: %s\n",
				avahi_strerror(avahi_client_errno(priv->client)));
		netresolve_backend_failed(priv->query);
	}

	priv->pending++;
}

void
cleanup(void *data)
{
	struct priv_avahi *priv = data;

	if (priv->client)
		avahi_client_free(priv->client);

	free(priv->name);
}

static struct priv_avahi *
setup(netresolve_query_t query, char **settings)
{
	struct priv_avahi *priv = netresolve_backend_new_priv(query, sizeof *priv, cleanup);
	int error;

	if (!priv)
		return NULL;;

	const char *name = netresolve_backend_get_nodename(query);
	priv->name = name ? strdup(name) : NULL;
	priv->family = netresolve_backend_get_family(query);

	priv->query = query;

	priv->poll_config.userdata = priv;
	priv->poll_config.watch_new = watch_new;
	priv->poll_config.watch_update = watch_update;
	//priv->poll_config.watch_get_events = watch_get_events;
	priv->poll_config.watch_free = watch_free;
	priv->poll_config.timeout_new = timeout_new;
	priv->poll_config.timeout_update = timeout_update;
	priv->poll_config.timeout_free = timeout_free;
	priv->client = avahi_client_new(&priv->poll_config, 0, client_callback, priv, &error);

	if (!priv->client) {
		error("Avahi client failure: %s", avahi_strerror(error));
		return NULL;
	}

	return priv;
}

void
query_forward(netresolve_query_t query, char **settings)
{
	struct priv_avahi *priv;

	if (!(priv = setup(query, settings)) || !priv->name) {
		netresolve_backend_failed(query);
		return;
	}

	if (priv->family == AF_INET || priv->family == AF_UNSPEC)
		lookup_host(priv, AVAHI_PROTO_INET);
	if (priv->family == AF_INET6 || priv->family == AF_UNSPEC)
		lookup_host(priv, AVAHI_PROTO_INET6);
}
