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
	ares_channel query;
	fd_set rfds;
	fd_set wfds;
	int nfds;
	int ptfd;
	struct ares_srv_reply *srv_reply;
};

static void
register_fds(netresolve_query_t query)
{
	struct priv_dns *priv = netresolve_backend_get_priv(query);
	fd_set rfds;
	fd_set wfds;
	int nfds, fd;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	nfds = ares_fds(priv->query, &rfds, &wfds);

	for (fd = 0; fd < nfds || fd < priv->nfds; fd++) {
		if (!FD_ISSET(fd, &rfds) && FD_ISSET(fd, &priv->rfds)) {
			FD_CLR(fd, &priv->rfds);
			netresolve_backend_watch_fd(query, fd, 0);
		} else if (FD_ISSET(fd, &rfds) && !FD_ISSET(fd, &priv->rfds)) {
			FD_SET(fd, &priv->rfds);
			netresolve_backend_watch_fd(query, fd, POLLIN);
		}
		if (!FD_ISSET(fd, &wfds) && FD_ISSET(fd, &priv->wfds)) {
			FD_CLR(fd, &priv->wfds);
			netresolve_backend_watch_fd(query, fd, 0);
		} else if (FD_ISSET(fd, &wfds) && !FD_ISSET(fd, &priv->wfds)) {
			FD_SET(fd, &priv->wfds);
			netresolve_backend_watch_fd(query, fd, POLLOUT);
		}
	}

	priv->nfds = nfds;

	if (!nfds)
		netresolve_backend_finished(query);
}

struct priv_address_lookup {
	netresolve_query_t query;
	struct ares_srv_reply *srv;
};

static void
host_callback(void *arg, int status, int timeouts, struct hostent *he)
{
	struct priv_address_lookup *lookup_data = arg;
	netresolve_query_t query = lookup_data->query;
	struct ares_srv_reply *srv = lookup_data->srv;
	struct priv_dns *priv = netresolve_backend_get_priv(query);
	int socktype = 0;
	int protocol = 0;
	int port = 0;
	int priority = 0;
	int weight = 0;
	int ttl = 0;

	if (srv) {
		socktype = netresolve_backend_get_socktype(query);
		protocol = netresolve_backend_get_protocol(query);
		port = srv->port;
		priority = srv->priority;
		weight = srv->weight;
	}

	switch (status) {
	case ARES_EDESTRUCTION:
		break;
	case ARES_SUCCESS:
		priv->ptfd = netresolve_backend_add_timeout(query, partial_timeout, 0);
		if (priv->ptfd == -1)
			error("timer: %s", strerror(errno));
		netresolve_backend_apply_hostent(query, he, socktype, protocol, port, priority, weight, ttl);
		break;
	default:
		error("ares: %s\n", ares_strerror(status));
	}
}

static void
start_address_lookup(netresolve_query_t query, struct ares_srv_reply *srv)
{
	struct priv_dns *priv = netresolve_backend_get_priv(query);
	int family = netresolve_backend_get_family(query);
	const char *node = netresolve_backend_get_nodename(query);
	struct priv_address_lookup *lookup_data = calloc(1, sizeof *lookup_data);

	if (!lookup_data) {
		netresolve_backend_failed(query);
		return;
	}

	lookup_data->query = query;

	if (srv) {
		node = srv->host;
		lookup_data->srv = srv;
	}

	if (family == AF_INET || family == AF_UNSPEC)
		ares_gethostbyname(priv->query, node, AF_INET, host_callback, lookup_data);
	if (family == AF_INET6 || family == AF_UNSPEC)
		ares_gethostbyname(priv->query, node, AF_INET6, host_callback, lookup_data);
}

static void
srv_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen)
{
	netresolve_query_t query = arg;
	struct priv_dns *priv = netresolve_backend_get_priv(query);

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
				start_address_lookup(query, reply);
		} else
			start_address_lookup(query, NULL);
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

static void
start_srv_lookup(netresolve_query_t query)
{
	struct priv_dns *priv = netresolve_backend_get_priv(query);
	char *name;

	if (asprintf(&name, "_%s._%s.%s",
			netresolve_backend_get_servname(query),
			protocol_to_string(netresolve_backend_get_protocol(query)),
			netresolve_backend_get_nodename(query)) == -1) {
		netresolve_backend_failed(query);
		return;
	}

	debug("looking up SRV %s\n", name);
	ares_query(priv->query, name, ns_c_in, ns_t_srv, srv_callback, query);
}

void
setup_forward(netresolve_query_t query, char **settings)
{
	const char *node = netresolve_backend_get_nodename(query);
	struct priv_dns *priv = netresolve_backend_new_priv(query, sizeof *priv);
	/* ares doesn't seem to accept const options */
	static struct ares_options options = {
		.flags = ARES_FLAG_NOSEARCH | ARES_FLAG_NOALIASES,
		.lookups = "b"
	};
	int status;

	if (!priv)
		goto fail;
	if (!node)
		goto fail;


	priv->ptfd = -1;

	FD_ZERO(&priv->rfds);
	FD_ZERO(&priv->wfds);

	status = ares_library_init(ARES_LIB_INIT_ALL);
	if (status != ARES_SUCCESS)
		goto fail_ares;
	status = ares_init_options(&priv->query, &options,
			ARES_OPT_FLAGS | ARES_OPT_LOOKUPS);
	if (status != ARES_SUCCESS)
		goto fail_ares;

	if (netresolve_backend_get_dns_srv_lookup(query))
		start_srv_lookup(query);
	else
		start_address_lookup(query, NULL);
	register_fds(query);

	return;
fail_ares:
	error("ares: %s\n", ares_strerror(status));
fail:
	netresolve_backend_failed(query);
}

void
dispatch(netresolve_query_t query, int fd, int events)
{
	struct priv_dns *priv = netresolve_backend_get_priv(query);

	int rfd = events & POLLIN ? fd : ARES_SOCKET_BAD;
	int wfd = events & POLLOUT ? fd : ARES_SOCKET_BAD;

	if (fd == priv->ptfd) {
		error("partial response used due to a timeout\n");
		netresolve_backend_finished(query);
		return;
	}

	ares_process_fd(priv->query, rfd, wfd);
	register_fds(query);
}

void
cleanup(netresolve_query_t query)
{
	struct priv_dns *priv = netresolve_backend_get_priv(query);
	int fd;

	for (fd = 0; fd < priv->nfds; fd++) {
		if (FD_ISSET(fd, &priv->rfds) || FD_ISSET(fd, &priv->wfds)) {
			FD_CLR(fd, &priv->rfds);
			netresolve_backend_watch_fd(query, fd, 0);
		}
	}

	if (priv->ptfd != -1)
		netresolve_backend_remove_timeout(query, priv->ptfd);
	if (priv->srv_reply)
		ares_free_data(priv->srv_reply);

	ares_destroy(priv->query);
	//ares_library_cleanup();
}
