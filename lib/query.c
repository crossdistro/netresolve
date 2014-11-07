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
	case NETRESOLVE_STATE_WAITING_MORE:
		return "waiting-more";
	case NETRESOLVE_STATE_RESOLVED:
		return "resolved";
	case NETRESOLVE_STATE_CONNECTING:
		return "connecting";
	case NETRESOLVE_STATE_DONE:
		return "done";
	case NETRESOLVE_STATE_ERROR:
		return "error";
	case NETRESOLVE_STATE_FAILED:
		return "failed";
	}

	abort();
}

static void
clear_timeout(netresolve_query_t query, int *tfd)
{
	if (*tfd == -1)
		return;

	netresolve_remove_timeout(query->channel, *tfd);
	*tfd = -1;
}

static void
cleanup_query(netresolve_query_t query)
{
	struct netresolve_backend *backend = query->backend ? *query->backend : NULL;

	clear_timeout(query, &query->delayed_fd);
	clear_timeout(query, &query->timeout_fd);
	clear_timeout(query, &query->partial_timeout_fd);

	if (backend && backend->data) {
		if (backend->cleanup)
			backend->cleanup(query);
		free(backend->data);
		backend->data = NULL;
	}
}

void
netresolve_query_set_state(netresolve_query_t query, enum netresolve_state state)
{
	enum netresolve_state old_state = query->state;

	if (state == old_state)
		return;

	query->state = state;

	debug_query(query, "state: %s -> %s", state_to_string(old_state), state_to_string(state));

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
		{
			struct netresolve_backend *backend = *query->backend;
			void (*setup)(netresolve_query_t query, char **settings);

			if (query->request.dns_srv_lookup && !query->request.protocol)
				query->request.protocol = IPPROTO_TCP;

			setup = backend->setup[query->request.type];
			if (setup) {
				setup(query, backend->settings + 1);
				if (query->state == NETRESOLVE_STATE_SETUP)
					netresolve_query_set_state(query, query->request.timeout ? NETRESOLVE_STATE_WAITING : NETRESOLVE_STATE_FAILED);
				if (query->state == NETRESOLVE_STATE_ERROR)
					netresolve_query_set_state(query, NETRESOLVE_STATE_FAILED);
			} else
				netresolve_query_set_state(query, NETRESOLVE_STATE_FAILED);
		}
		break;
	case NETRESOLVE_STATE_WAITING:
		if (query->request.timeout > 0)
			query->timeout_fd = netresolve_add_timeout_ms(query->channel, query->request.timeout);
		break;
	case NETRESOLVE_STATE_WAITING_MORE:
		if (query->request.partial_timeout == 0)
			netresolve_query_set_state(query, NETRESOLVE_STATE_CONNECTING);
		if (query->request.partial_timeout > 0)
			query->partial_timeout_fd = netresolve_add_timeout_ms(query->channel, query->request.partial_timeout);
		break;
	case NETRESOLVE_STATE_RESOLVED:
		if (old_state == NETRESOLVE_STATE_SETUP) {
			if ((query->delayed_fd = eventfd(1, EFD_NONBLOCK)) == -1) {
				error("can't create eventfd");
				abort();
			}
			netresolve_watch_fd(query->channel, query->delayed_fd, POLLIN);
		}
		break;
	case NETRESOLVE_STATE_CONNECTING:
		cleanup_query(query);

		/* Restart with the next *mandatory* backend. */
		while (*++query->backend) {
			if ((*query->backend)->mandatory) {
				netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);
				break;
			}
		}

		if (query->channel->callbacks.on_connect) {
			netresolve_connect_start(query);
		} else
			netresolve_query_set_state(query, NETRESOLVE_STATE_DONE);
		break;
	case NETRESOLVE_STATE_DONE:
		if (query->channel->callbacks.on_connect)
			netresolve_connect_cleanup(query);
		if (query->channel->callbacks.on_success)
			query->channel->callbacks.on_success(query, query->channel->callbacks.user_data);
		break;
	case NETRESOLVE_STATE_ERROR:
		break;
	case NETRESOLVE_STATE_FAILED:
		if (query->response.pathcount)
			error("Failed reply has data.");

		cleanup_query(query);

		/* Restart with the next backend. */
		if (*++query->backend)
			netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);
		break;
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
		netresolve_query_set_state(query, NETRESOLVE_STATE_FAILED);
		return query;
	}

	query->first_connect_timeout = -1;
	query->delayed_fd = -1;
	query->timeout_fd = -1;
	query->partial_timeout_fd = -1;
	query->backend = channel->backends;
	memcpy(&query->request, &channel->request, sizeof channel->request);

	query->request.type = type;

	debug_query(query, "created", type);

	return query;
}

void
netresolve_query_start(netresolve_query_t query)
{
	netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);
}

static bool
dispatch_timeout(netresolve_query_t query, int *tfd, enum netresolve_state state, int fd, int events)
{
	if (fd != *tfd)
		return false;

	clear_timeout(query, tfd);
	netresolve_query_set_state(query, state);

	return true;
}

/* netresolve_query_dispatch:
 *
 * This internal function is called by the netresolve channel object to hand
 * over file descriptor events to a query. The query must return `false` if
 * it didn't handle the query so that the channel can pass it to the next
 * query.
 */
bool
netresolve_query_dispatch(netresolve_query_t query, int fd, int events)
{
	struct netresolve_backend *backend = query->backend ? *query->backend : NULL;

	debug_query(query, "dispatching: fd=%d events=%d", fd, events);

	switch (query->state) {
	case NETRESOLVE_STATE_WAITING_MORE:
		if (dispatch_timeout(query, &query->partial_timeout_fd, NETRESOLVE_STATE_CONNECTING, fd, events)) {
			debug_query(query, "partial result timed out");
			return true;
		}
		/* fall through */
	case NETRESOLVE_STATE_WAITING:
		if (dispatch_timeout(query, &query->timeout_fd, NETRESOLVE_STATE_FAILED, fd, events)) {
			debug_query(query, "timed out");
			return true;
		}
		if (backend && backend->dispatch) {
			backend->dispatch(query, fd, events);
			if (query->state == NETRESOLVE_STATE_RESOLVED)
				netresolve_query_set_state(query, NETRESOLVE_STATE_CONNECTING);
			if (query->state == NETRESOLVE_STATE_ERROR)
				netresolve_query_set_state(query, NETRESOLVE_STATE_FAILED);
			return true;
		}
		return false;
	case NETRESOLVE_STATE_RESOLVED:
		return dispatch_timeout(query, &query->delayed_fd, NETRESOLVE_STATE_CONNECTING, fd, events);
	case NETRESOLVE_STATE_CONNECTING:
		return netresolve_connect_dispatch(query, fd, events);
	default:
		return false;
	}
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

	cleanup_query(query);

	netresolve_query_set_state(query, NETRESOLVE_STATE_NONE);

	for (i = 0; i < channel->nqueries; i++)
		if (channel->queries[i] == query)
			break;
	if (i < channel->nqueries)
		memmove(&channel->queries[i], &channel->queries[i+1], --channel->nqueries - i);
	channel->queries = realloc(channel->queries, channel->nqueries * sizeof *channel->queries);

	free(query);
}

/* netresolve_query_set_user_data:
 *
 * Run this function just after starting the query to attach a pointer to some
 * data that you will later retrieve with `netresolve_query_get_user_data()`.
 */
void
netresolve_query_set_user_data(netresolve_query_t query, void *user_data)
{
	query->user_data = user_data;
}

/* netresolve_query_get_user_data:
 *
 * Retrieve a pointer previously attached using `netresolve_query_set_user_data()`.
 */
void *
netresolve_query_get_user_data(netresolve_query_t query)
{
	return query->user_data;
}

/* netresolve_query_get_count:
 *
 * Forward query: Retrieve a number of address objects available in the query
 * result.
 */
size_t
netresolve_query_get_count(netresolve_query_t query)
{
	return query->response.pathcount;
}

/* netresolve_query_get_node_info:
 *
 * Forward query: Retrieve node related address information.
 */
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

/* netresolve_query_get_service_info:
 *
 * Forward query: Retrieve service related address information.
 */
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

/* netresolve_query_get_aux_info:
 *
 * Forward query: Retreive auxiliary address information.
 */
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
		*ttl = query->request.clamp_ttl >= 0 ? query->request.clamp_ttl : query->response.paths[idx].ttl;
}

/* netresolve_query_get_node_name:
 *
 * Forward query: Retrieve the canonical name.
 *
 * Reverse query: Retrieve the node name.
 */
const char *
netresolve_query_get_node_name(const netresolve_query_t query)
{
	if (query->response.nodename)
		return query->response.nodename;
	if (query->state != NETRESOLVE_STATE_DONE)
		return NULL;
	if (query->request.type != NETRESOLVE_REQUEST_FORWARD)
		return NULL;
	return query->request.nodename ? query->request.nodename : "localhost";
}

/* netresolve_query_get_service_name:
 *
 * Reverse query: Retrieve the service name.
 */
const char *
netresolve_query_get_service_name(const netresolve_query_t query)
{
	return query->response.servname;
}

/* netresolve_query_get_dns_answer:
 *
 * DNS query: Retrieve the DNS answer.
 */
const void *
netresolve_query_get_dns_answer(const netresolve_query_t query, size_t *size)
{
	if (size)
		*size = query->response.dns.length;

	return &query->response.dns.answer;
}
