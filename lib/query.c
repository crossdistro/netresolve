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
#include <netresolve-private.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <sys/eventfd.h>

const char *
netresolve_query_state_to_string(enum netresolve_state state)
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

	netresolve_remove_timeout(query, *tfd);
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

	debug_query(query, "state: %s -> %s",
			netresolve_query_state_to_string(old_state), netresolve_query_state_to_string(state));

	query->state = state;

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
			query->timeout_fd = netresolve_add_timeout_ms(query, query->request.timeout);
		break;
	case NETRESOLVE_STATE_WAITING_MORE:
		if (query->request.partial_timeout == 0)
			netresolve_query_set_state(query, NETRESOLVE_STATE_DONE);
		if (query->request.partial_timeout > 0)
			query->partial_timeout_fd = netresolve_add_timeout_ms(query, query->request.partial_timeout);
		break;
	case NETRESOLVE_STATE_RESOLVED:
		if (old_state == NETRESOLVE_STATE_SETUP) {
			if ((query->delayed_fd = eventfd(1, EFD_NONBLOCK)) == -1) {
				error("can't create eventfd");
				abort();
			}
			netresolve_watch_fd(query, query->delayed_fd, POLLIN);
		}
		break;
	case NETRESOLVE_STATE_DONE:
		cleanup_query(query);

		/* Restart with the next *mandatory* backend. */
		while (*++query->backend) {
			if ((*query->backend)->mandatory) {
				netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);
				break;
			}
		}

		if (query->callback)
			query->callback(query, query->user_data);
		break;
	case NETRESOLVE_STATE_ERROR:
		break;
	case NETRESOLVE_STATE_FAILED:
		if (query->response.pathcount)
			error("Failed reply has data.");

		cleanup_query(query);

		/* Restart with the next backend. */
		if (*++query->backend) {
			netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);
			break;
		}

		if (query->callback)
			query->callback(query, query->user_data);
		break;
	}
}

netresolve_query_t
netresolve_query_new(netresolve_t context, enum netresolve_request_type type)
{
	struct netresolve_query *queries = &context->queries;
	netresolve_query_t query;

	if (!(query = calloc(1, sizeof *query)))
		return NULL;

	query->previous = queries->previous;
	query->next = queries;
	query->previous->next = query->next->previous = query;

	query->context = context;
	query->sources.previous = query->sources.next = &query->sources;

	if (!context->backends)
		netresolve_set_backend_string(context, secure_getenv("NETRESOLVE_BACKENDS"));
	if (!context->backends || !*context->backends)
		abort();

	query->delayed_fd = -1;
	query->timeout_fd = -1;
	query->partial_timeout_fd = -1;
	query->backend = context->backends;
	memcpy(&query->request, &context->request, sizeof context->request);

	query->request.type = type;

	debug_query(query, "created", type);

	return query;
}

netresolve_query_t
netresolve_query( netresolve_t context, netresolve_query_callback callback, void *user_data,
		enum netresolve_option type, ...)
{
	static netresolve_t default_context = NULL;
	netresolve_query_t query;
	va_list ap;

	/* Use a static context if none is provided. */
	if (!context) {
		if (!default_context)
			default_context = netresolve_context_new();
		context = default_context;
	}
	if (!context)
		return NULL;

	if (!(query = netresolve_query_new(context, type)))
		return NULL;

	query->callback = callback;
	query->user_data = user_data;

	va_start(ap, type);
	if (!netresolve_request_set_options_from_va(&query->request, ap)) {
		netresolve_query_free(query);
		va_end(ap);
		return NULL;
	}
	va_end(ap);

	if (context->config.force_family)
		query->request.family = context->config.force_family;

	/* Install default callbacks for first query in blocking mode. */
	if (!context->callbacks.watch_fd)
		netresolve_epoll_install(context, &context->epoll, NULL);

	netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);

	/* Wait for the context in blocking mode. */
	if (context->callbacks.user_data == &context->epoll)
		netresolve_epoll_wait(context);

	return query;
}

netresolve_query_t
netresolve_query_forward(netresolve_t context,
		const char *nodename, const char *servname,
		netresolve_query_callback callback, void *user_data)
{
	return netresolve_query(context, callback, user_data,
			NETRESOLVE_REQUEST_FORWARD,
			NETRESOLVE_OPTION_NODE_NAME, nodename,
			NETRESOLVE_OPTION_SERVICE_NAME, servname,
			NULL);
}

netresolve_query_t
netresolve_query_reverse(netresolve_t context,
		int family, const void *address, int ifindex, int protocol, int port,
		netresolve_query_callback callback, void *user_data)
{
	enum netresolve_option address_option;

	switch (family) {
	case AF_INET:
		address_option = NETRESOLVE_OPTION_IP4_ADDRESS;
		break;
	case AF_INET6:
		address_option = NETRESOLVE_OPTION_IP6_ADDRESS;
		break;
	default:
		return NULL;
	}

	return netresolve_query(context, callback, user_data,
			NETRESOLVE_REQUEST_REVERSE,
			address_option, address,
			NETRESOLVE_OPTION_IFINDEX, ifindex,
			NETRESOLVE_OPTION_PROTOCOL, protocol,
			NETRESOLVE_OPTION_PORT, port,
			NULL);
}

netresolve_query_t
netresolve_query_dns(netresolve_t context,
		const char *dname, int cls, int type,
		netresolve_query_callback callback, void *user_data)
{
	return netresolve_query(context, callback, user_data,
			NETRESOLVE_REQUEST_DNS,
			NETRESOLVE_OPTION_DNS_NAME, dname,
			NETRESOLVE_OPTION_DNS_CLASS, cls,
			NETRESOLVE_OPTION_DNS_TYPE, type,
			NULL);
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
 * This internal function is called by the netresolve context object to hand
 * over file descriptor events to a query. The query must return `false` if
 * it didn't handle the query so that the context can pass it to the next
 * query.
 */
bool
netresolve_query_dispatch(netresolve_query_t query, int fd, int events)
{
	struct netresolve_backend *backend = query->backend ? *query->backend : NULL;

	switch (query->state) {
	case NETRESOLVE_STATE_WAITING_MORE:
		if (dispatch_timeout(query, &query->partial_timeout_fd, NETRESOLVE_STATE_DONE, fd, events)) {
			debug_query(query, "partial result timed out");
			return true;
		}
		/* fall through */
	case NETRESOLVE_STATE_WAITING:
		if (dispatch_timeout(query, &query->timeout_fd, NETRESOLVE_STATE_FAILED, fd, events)) {
			debug_query(query, "result timed out");
			return true;
		}
		if (backend && backend->dispatch) {
			backend->dispatch(query, fd, events);
			if (query->state == NETRESOLVE_STATE_RESOLVED)
				netresolve_query_set_state(query, NETRESOLVE_STATE_DONE);
			if (query->state == NETRESOLVE_STATE_ERROR)
				netresolve_query_set_state(query, NETRESOLVE_STATE_FAILED);
			return true;
		}
		debug_query(query, "event received, not dispatched: fd=%d events=%d", fd, events);
		return false;
	case NETRESOLVE_STATE_RESOLVED:
		return dispatch_timeout(query, &query->delayed_fd, NETRESOLVE_STATE_DONE, fd, events);
	case NETRESOLVE_STATE_DONE:
		return netresolve_connect_dispatch(query, fd, events);
	default:
		break;
	}

	debug_query(query, "unexpected event: fd=%d events=%d", fd, events);
	return false;
}

/* netresolve_query_free:
 *
 * Call this function when you are finished with the netresolve query and
 * don't need to access it any more. It cancels the query if it hasn't been
 * finished yet, and performs internal cleanups. Don't use the query handle
 * after calling it.
 */
void
netresolve_query_free(netresolve_query_t query)
{
	cleanup_query(query);

	netresolve_query_set_state(query, NETRESOLVE_STATE_NONE);

	query->previous->next = query->next;
	query->next->previous = query->previous;

	free(query->request.nodename);
	free(query->request.servname);
	free(query->request.dns_name);
	free(query);
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
	return query->request.nodename ?: "localhost";
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

	return query->response.dns.answer;
}

bool
netresolve_query_get_secure(const netresolve_query_t query)
{
	return query->response.security == NETRESOLVE_SECURITY_SECURE;
}
