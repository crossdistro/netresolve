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
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

#include "netresolve-private.h"

static const char *
state_to_string(enum netresolve_state state)
{
	switch (state) {
	case NETRESOLVE_STATE_NONE:
		return "none";
	case NETRESOLVE_STATE_SETUP:
		return "setup";
	case NETRESOLVE_STATE_WAITING:
		return "waiting";
	case NETRESOLVE_STATE_FINISHED:
		return "finished";
	case NETRESOLVE_STATE_FAILED:
		return "failed";
	default:
		/* Shouldn't happen. */
		return "UNKNOWN";
	}
}

void
netresolve_query_set_state(netresolve_query_t query, enum netresolve_state state)
{
	enum netresolve_state old_state = query->state;

	query->state = state;

	debug("state: %s -> %s", state_to_string(old_state), state_to_string(state));

	/* Leaving state... */
	switch (old_state) {
	case NETRESOLVE_STATE_WAITING:
		break;
	default:
		break;
	}

	/* Delaying state... */
	switch (state) {
	case NETRESOLVE_STATE_FINISHED:
	case NETRESOLVE_STATE_FAILED:
		if (old_state != NETRESOLVE_STATE_WAITING) {
			if ((query->delayed_fd = eventfd(1, EFD_NONBLOCK)) == -1) {
				error("can't create eventfd");
				break;
			}
			query->delayed_state = state;
			state = query->state = NETRESOLVE_STATE_WAITING;
			netresolve_watch_fd(query->channel, query->delayed_fd, POLLIN);
			break;
		}
		break;
	default:
		break;
	}

	/* Entering state... */
	switch (state) {
	case NETRESOLVE_STATE_NONE:
		free(query->request.dns_name);
		free(query->response.paths);
		free(query->response.nodename);
		free(query->response.servname);
		netresolve_service_list_free(query->services);
		memset(&query->response, 0, sizeof query->response);
		break;
	case NETRESOLVE_STATE_SETUP:
		netresolve_query_setup(query);
	case NETRESOLVE_STATE_WAITING:
		break;
	case NETRESOLVE_STATE_FINISHED:
		if (!query->response.nodename)
			query->response.nodename = query->request.nodename ? strdup(query->request.nodename) : strdup("localhost");
		if (query->channel->callbacks.on_connect)
			netresolve_connect_cleanup(query);
		if (query->channel->callbacks.on_success)
			query->channel->callbacks.on_success(query, query->channel->callbacks.user_data);
		break;
	case NETRESOLVE_STATE_FAILED:
		if (query->channel->callbacks.on_success)
			query->channel->callbacks.on_success(query, query->channel->callbacks.user_data);
		break;
	}
}

static int
state_to_errno(enum netresolve_state state)
{
	switch (state) {
	case NETRESOLVE_STATE_WAITING:
		return EWOULDBLOCK;
	case NETRESOLVE_STATE_FINISHED:
		return 0;
	case NETRESOLVE_STATE_FAILED:
		return ENODATA;
	default:
		/* Shouldn't happen. */
		return -1;
	}
}

netresolve_query_t
netresolve_query_new(netresolve_t channel, enum netresolve_request_type type)
{
	netresolve_query_t query;
	netresolve_query_t *queries;

	if (!(query = calloc(1, sizeof *query)))
		return NULL;
	if (!(queries = realloc(channel->queries, ++channel->nqueries * sizeof *queries))) {
		free(query);
		return NULL;
	}

	query->channel = channel;
	channel->queries = queries;
	channel->queries[channel->nqueries - 1] = query;

	if (!channel->backends)
		netresolve_set_backend_string(channel, secure_getenv("NETRESOLVE_BACKENDS"));
	if (!channel->backends || !*channel->backends) {
		netresolve_query_set_state(query, NETRESOLVE_STATE_FINISHED);
		return query;
	}

	query->first_connect_timeout = -1;
	query->backend = channel->backends;
	memcpy(&query->request, &channel->request, sizeof channel->request);

	query->request.type = type;

	return query;
}

void
netresolve_query_setup(netresolve_query_t query)
{
	struct netresolve_backend *backend = *query->backend;
	void (*setup)(netresolve_query_t query, char **settings);

	debug("starting backend: %s", backend->settings[0]);
	assert(query->state == NETRESOLVE_STATE_SETUP);

	setup = backend->setup[query->request.type];
	if (setup) {
		setup(query, backend->settings + 1);
		if (query->state == NETRESOLVE_STATE_SETUP)
			netresolve_query_set_state(query, NETRESOLVE_STATE_WAITING);
	} else
		netresolve_backend_failed(query);
}

bool
netresolve_query_dispatch_fd(netresolve_query_t query, int fd, int events)
{
	struct netresolve_backend *backend = *query->backend;

	if (query->state != NETRESOLVE_STATE_WAITING)
		return false;

	if (query->delayed_state && fd == query->delayed_fd) {
		netresolve_query_apply_delayed_state(query);
		return true;
	}

	if (!backend && netresolve_connect_dispatch(query, fd, events))
		return true;

	if (backend && backend->dispatch) {
		backend->dispatch(query, fd, events);
		return true;
	}

	return false;
}

void
netresolve_query_cleanup(netresolve_query_t query)
{
	struct netresolve_backend *backend = *query->backend;

	if (backend && backend->data) {
		if (backend->cleanup)
			backend->cleanup(query);
		free(backend->data);
		backend->data = NULL;
	}
}

void
netresolve_query_finished(netresolve_query_t query)
{
	if (!*query->backend) {
		error("Out of order backend callback.");
		goto fail;
	}

	netresolve_query_cleanup(query);

	/* Restart with the next *mandatory* backend. */
	while (*++query->backend) {
		if ((*query->backend)->mandatory) {
			netresolve_query_setup(query);
			return;
		}
	}

	if (query->channel->callbacks.on_connect) {
		netresolve_connect_start(query);
		return;
	}

	netresolve_query_set_state(query, NETRESOLVE_STATE_FINISHED);
	return;

fail:
	netresolve_query_set_state(query, NETRESOLVE_STATE_FAILED);
}

void
netresolve_query_failed(netresolve_query_t query)
{
	if (!*query->backend) {
		error("Out of order backend callback.");
		goto fail;
	}

	if (query->response.pathcount)
		error("Non-empty failed reply.");

	debug("failed");
	netresolve_query_cleanup(query);

	/* Restart with the next backend. */
	if (*++query->backend) {
		netresolve_query_setup(query);
		return;
	}

fail:
	netresolve_query_set_state(query, NETRESOLVE_STATE_FAILED);
}

void
netresolve_query_clear_delayed_state(netresolve_query_t query)
{
	query->delayed_state = NETRESOLVE_STATE_NONE;
	netresolve_watch_fd(query->channel, query->delayed_fd, 0);
	close(query->delayed_fd);
	query->delayed_fd = -1;
}

void
netresolve_query_apply_delayed_state(netresolve_query_t query)
{
	enum netresolve_state state = query->delayed_state;

	netresolve_query_clear_delayed_state(query);

	netresolve_query_set_state(query, state);
}

netresolve_query_t
netresolve_query_run(netresolve_query_t query)
{
	netresolve_t channel = query->channel;

	netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);

	/* Blocking mode. */
	if (!channel->fd_callbacks.watch_fd)
		while (query->state == NETRESOLVE_STATE_WAITING)
			netresolve_epoll(channel, true);

	errno = state_to_errno(query->state);
	return query;
}

/* netresolve_query_done:
 *
 * Call this function when you are finished with the netresolve query and
 * don't need to access it any more. It cancels the query if it hasn't been
 * finished yet, and performs internal cleanups. Don't use the query handle
 * after calling it.
 */
void
netresolve_query_done(netresolve_query_t query)
{
	netresolve_t channel = query->channel;
	int i;

	if (query->delayed_state)
		netresolve_query_clear_delayed_state(query);

	netresolve_query_set_state(query, NETRESOLVE_STATE_NONE);

	for (i = 0; i < channel->nqueries; i++)
		if (channel->queries[i] == query)
			break;
	if (i < channel->nqueries)
		memmove(&channel->queries[i], &channel->queries[i+1], --channel->nqueries - i);
	channel->queries = realloc(channel->queries, channel->nqueries * sizeof *channel->queries);

	free(query);
}

void
netresolve_query_set_user_data(netresolve_query_t query, void *user_data)
{
	query->user_data = user_data;
}

void *
netresolve_query_get_user_data(netresolve_query_t query)
{
	return query->user_data;
}

size_t
netresolve_query_get_count(netresolve_query_t query)
{
	return query->response.pathcount;
}

void
netresolve_query_get_node_info(netresolve_query_t query, size_t idx,
		int *family, const void **address, int *ifindex)
{
	assert (idx < query->response.pathcount);

	if (family)
		*family = query->response.paths[idx].node.family;
	if (address)
		*address = &query->response.paths[idx].node.address;
	if (ifindex)
		*ifindex = query->response.paths[idx].node.ifindex;
}

void
netresolve_query_get_service_info(netresolve_query_t query, size_t idx,
		int *socktype, int *protocol, int *port)
{
	assert (idx < query->response.pathcount);

	if (socktype)
		*socktype = query->response.paths[idx].service.socktype;
	if (protocol)
		*protocol = query->response.paths[idx].service.protocol;
	if (port)
		*port = query->response.paths[idx].service.port;
}

void
netresolve_query_get_aux_info(netresolve_query_t query, size_t idx,
		int *priority, int *weight, int *ttl)
{
	assert (idx < query->response.pathcount);

	if (priority)
		*priority = query->response.paths[idx].priority;
	if (weight)
		*weight = query->response.paths[idx].weight;
	if (ttl)
		*ttl = query->response.paths[idx].ttl;
}

const char *
netresolve_query_get_node_name(const netresolve_query_t query)
{
	return query->response.nodename;
}

const char *
netresolve_query_get_service_name(const netresolve_query_t query)
{
	return query->response.servname;
}

const struct sockaddr *
netresolve_query_get_sockaddr(netresolve_query_t query, size_t idx,
		socklen_t *salen, int *socktype, int *protocol, int32_t *ttl)
{
	int family, ifindex, port;
	const void *address;

	netresolve_query_get_node_info(query, idx, &family, &address, &ifindex);
	netresolve_query_get_service_info(query, idx, socktype, protocol, &port);
	netresolve_query_get_aux_info(query, idx, NULL, NULL, ttl);

	if (!address)
		return NULL;

	memset(&query->sa_buffer, 0, sizeof query->sa_buffer);

	switch (family) {
	case AF_INET:
		query->sa_buffer.sin.sin_family = family;
		query->sa_buffer.sin.sin_port = htons(port);
		query->sa_buffer.sin.sin_addr = *(struct in_addr *) address;
		if (salen)
			*salen = sizeof query->sa_buffer.sin;
		break;
	case AF_INET6:
		query->sa_buffer.sin6.sin6_family = family;
		query->sa_buffer.sin6.sin6_port = htons(port);
		query->sa_buffer.sin6.sin6_scope_id = ifindex;
		query->sa_buffer.sin6.sin6_addr = *(struct in6_addr *) address;
		if (salen)
			*salen = sizeof query->sa_buffer.sin6;
		break;
	default:
		return NULL;
	}

	return &query->sa_buffer.sa;
}

const void *
netresolve_query_get_dns_answer(const netresolve_query_t query, size_t *size)
{
	if (size)
		*size = query->response.dns.length;

	return &query->response.dns.answer;
}
