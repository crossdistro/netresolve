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
#include <ares.h>
#include <arpa/nameser.h>
#include <stdlib.h>
#include <stdio.h>

/* A timeout starting when the first successful answer has been received. */
static int partial_timeout = 5;

struct priv_dns {
	ares_channel channel;
	fd_set rfds;
	fd_set wfds;
	int nfds;
	int ptfd;
	struct ares_srv_reply *srv_reply;
};

void
register_fds(netresolve_backend_t resolver)
{
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);
	fd_set rfds;
	fd_set wfds;
	int nfds, fd;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	nfds = ares_fds(priv->channel, &rfds, &wfds);

	for (fd = 0; fd < nfds || fd < priv->nfds; fd++) {
		if (!FD_ISSET(fd, &rfds) && FD_ISSET(fd, &priv->rfds)) {
			FD_CLR(fd, &priv->rfds);
			netresolve_backend_watch_fd(resolver, fd, 0);
		} else if (FD_ISSET(fd, &rfds) && !FD_ISSET(fd, &priv->rfds)) {
			FD_SET(fd, &priv->rfds);
			netresolve_backend_watch_fd(resolver, fd, POLLIN);
		}
		if (!FD_ISSET(fd, &wfds) && FD_ISSET(fd, &priv->wfds)) {
			FD_CLR(fd, &priv->wfds);
			netresolve_backend_watch_fd(resolver, fd, 0);
		} else if (FD_ISSET(fd, &wfds) && !FD_ISSET(fd, &priv->wfds)) {
			FD_SET(fd, &priv->wfds);
			netresolve_backend_watch_fd(resolver, fd, POLLOUT);
		}
	}

	priv->nfds = nfds;

	if (!nfds)
		netresolve_backend_finished(resolver);
}

struct priv_address_lookup {
	netresolve_backend_t resolver;
	struct ares_srv_reply *srv;
};

void
host_callback(void *arg, int status, int timeouts, struct hostent *he)
{
	struct priv_address_lookup *lookup_data = arg;
	netresolve_backend_t resolver = lookup_data->resolver;
	struct ares_srv_reply *srv = lookup_data->srv;
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);
	int socktype = -1;
	int protocol = -1;
	int port = -1;
	int priority = 0;
	int weight = 0;

	if (srv) {
		socktype = netresolve_backend_get_socktype(resolver);
		protocol = netresolve_backend_get_protocol(resolver);
		port = srv->port;
		priority = srv->priority;
		weight = srv->weight;
	}

	switch (status) {
	case ARES_EDESTRUCTION:
		break;
	case ARES_SUCCESS:
		priv->ptfd = netresolve_backend_watch_timeout(resolver, partial_timeout, 0);
		if (priv->ptfd == -1)
			error("timer: %s", strerror(errno));
		netresolve_backend_apply_hostent(resolver, he, socktype, protocol, port, priority, weight);
		break;
	default:
		error("ares: %s\n", ares_strerror(status));
	}
}

void
start_address_lookup(netresolve_backend_t resolver, struct ares_srv_reply *srv)
{
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);
	int family = netresolve_backend_get_family(resolver);
	const char *node = netresolve_backend_get_node(resolver);
	struct priv_address_lookup *lookup_data = calloc(1, sizeof *lookup_data);

	if (!lookup_data) {
		netresolve_backend_failed(resolver);
		return;
	}

	lookup_data->resolver = resolver;

	if (srv) {
		node = srv->host;
		lookup_data->srv = srv;
	}

	if (family == AF_INET || family == AF_UNSPEC)
		ares_gethostbyname(priv->channel, node, AF_INET, host_callback, lookup_data);
	if (family == AF_INET6 || family == AF_UNSPEC)
		ares_gethostbyname(priv->channel, node, AF_INET6, host_callback, lookup_data);
}

static void
srv_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen)
{
	netresolve_backend_t resolver = arg;
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);

	switch (status) {
	case ARES_EDESTRUCTION:
		break;
	case ARES_SUCCESS:
		status = ares_parse_srv_reply(abuf, alen, &priv->srv_reply);
		if (status)
			error("ares: SRV lookup failed: %s", ares_strerror(status));
		if (priv->srv_reply) {
			struct ares_srv_reply *reply;

			for (reply = priv->srv_reply; reply; reply = reply->next)
				start_address_lookup(resolver, reply);
		} else
			start_address_lookup(resolver, NULL);
		break;
	default:
		error("ares: %s\n", ares_strerror(status));
	}
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

void
start_srv_lookup(netresolve_backend_t resolver)
{
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);
	char *name;

	if (asprintf(&name, "_%s._%s.%s",
			netresolve_backend_get_service(resolver),
			protocol_to_string(netresolve_backend_get_protocol(resolver)),
			netresolve_backend_get_node(resolver)) == -1) {
		netresolve_backend_failed(resolver);
		return;
	}

	debug("looking up SRV %s\n", name);
	ares_query(priv->channel, name, ns_c_in, ns_t_srv, srv_callback, resolver);
}

void
start(netresolve_backend_t resolver, char **settings)
{
	struct priv_dns *priv = netresolve_backend_new_priv(resolver, sizeof *priv);
	int status;

	if (!priv)
		goto fail;

	priv->ptfd = -1;

	FD_ZERO(&priv->rfds);
	FD_ZERO(&priv->wfds);

	status = ares_library_init(ARES_LIB_INIT_ALL);
	if (status != ARES_SUCCESS)
		goto fail_ares;
	status = ares_init(&priv->channel);
	if (status != ARES_SUCCESS)
		goto fail_ares;

	if (netresolve_backend_get_dns_srv_lookup(resolver))
		start_srv_lookup(resolver);
	else
		start_address_lookup(resolver, NULL);
	register_fds(resolver);

	return;
fail_ares:
	error("ares: %s\n", ares_strerror(status));
fail:
	netresolve_backend_failed(resolver);
}

void
dispatch(netresolve_backend_t resolver, int fd, int events)
{
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);

	int rfd = events & POLLIN ? fd : ARES_SOCKET_BAD;
	int wfd = events & POLLOUT ? fd : ARES_SOCKET_BAD;

	if (fd == priv->ptfd) {
		error("partial response used due to a timeout\n");
		netresolve_backend_finished(resolver);
		return;
	}

	ares_process_fd(priv->channel, rfd, wfd);
	register_fds(resolver);
}

void
cleanup(netresolve_backend_t resolver)
{
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);
	int fd;

	for (fd = 0; fd < priv->nfds; fd++) {
		if (FD_ISSET(fd, &priv->rfds) || FD_ISSET(fd, &priv->wfds)) {
			FD_CLR(fd, &priv->rfds);
			netresolve_backend_watch_fd(resolver, fd, 0);
		}
	}

	if (priv->ptfd != -1)
		netresolve_backend_drop_timeout(resolver, priv->ptfd);
	if (priv->srv_reply)
		ares_free_data(priv->srv_reply);

	ares_destroy(priv->channel);
	//ares_library_cleanup();
}
