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
#include <unistd.h>

#include "netresolve-private.h"

#define PRIORITY_TIMEOUT 1

struct netresolve_socket {
	netresolve_query_t query;
	netresolve_socket_callback_t callback;
	void *user_data;
	int flags;
	netresolve_timeout_t priority_timeout;
	bool skip_scheduled;
};

static void pickup_connected_socket(struct netresolve_socket *priv);

static void
connection_callback(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data)
{
	struct netresolve_socket *priv = query->user_data;
	struct netresolve_path *path = data;

	assert(fd == path->socket.fd);
	assert(events & POLLOUT);
	assert(path->socket.state == SOCKET_STATE_SCHEDULED);
	assert(path->socket.watch);

	socklen_t len = sizeof(errno);
	getsockopt(path->socket.fd, SOL_SOCKET, SO_ERROR, &errno, &len);

	debug_query(query, "socket: connection %s", errno ? "failed" : "succeeded");

	netresolve_watch_remove(priv->query, path->socket.watch, !!errno);

	path->socket.state = errno ? SOCKET_STATE_DONE : SOCKET_STATE_READY;
	path->socket.watch = NULL;

	/* See whether we can pass back a ready connection. */
	pickup_connected_socket(priv);
}

void
timeout_callback(netresolve_query_t query, netresolve_timeout_t timeout, void *data)
{
	struct netresolve_socket *priv = data;

	debug_query(query, "socket: priority timeout occured");

	priv->skip_scheduled = true;

	pickup_connected_socket(priv);
}

static void
setup_socket(struct netresolve_socket *priv, struct netresolve_path *path)
{
	static const int flags = O_NONBLOCK;
	int socktype;
	int protocol;
	const struct sockaddr *sa;
	socklen_t salen;
	int idx = path - priv->query->response.paths;

	assert(path->socket.state == SOCKET_STATE_NONE);

	sa = netresolve_query_get_sockaddr(priv->query, idx, &salen, &socktype, &protocol, NULL);
	if (!sa)
		goto fail;
	path->socket.fd = socket(sa->sa_family, socktype | flags, protocol);
	if (path->socket.fd == -1)
		goto fail;

	errno = 0;
	if (connect(path->socket.fd, sa, salen) == -1 && errno != EINPROGRESS)
		goto fail_connect;

	if (errno) {
		debug_query(priv->query, "socket: connection %d via %s scheduled", idx, sa->sa_family == AF_INET ? "IPv4" : "IPv6");

		path->socket.state = SOCKET_STATE_SCHEDULED;
		path->socket.watch = netresolve_watch_add(priv->query, path->socket.fd, POLLOUT, connection_callback, path);
	} else {
		/* FIXME: Is it at all possible for non-blocking sockets to connect
		 * immediately?
		 */
		debug_query(priv->query, "socket: connection %d via %s immediately succeeded", idx, sa->sa_family == AF_INET ? "IPv4" : "IPv6");

		path->socket.state = SOCKET_STATE_READY;
		pickup_connected_socket(priv);
	}

	return;
fail_connect:
	close(path->socket.fd);
	path->socket.fd = -1;
fail:
	path->socket.state = SOCKET_STATE_DONE;

	error("socket: connection %d failed", idx);
}

static void
enable_sockets(struct netresolve_socket *priv)
{
	netresolve_query_t query = priv->query;
	int ip4 = 0, ip6 = 0;

	/* Attempt to connect to one IPv4 and one IPv6 address in parallel. It is a
	 * „happy eyeballs“ style optimization that removes a delay when packets
	 * of the preferred protocol (typically IPvy) are dropped on the way which
	 * is a very common network misconfiguration.
	 */
	for (int i = 0; i < query->response.pathcount; i++) {
		struct netresolve_path *path = &query->response.paths[i];

		/* Skip already attempted sockets. */
		if (path->socket.state == SOCKET_STATE_DONE)
			continue;

		/* Skip all but first address for each protocol. */
		if (path->node.family == AF_INET && ip4++)
			continue;
		if (path->node.family == AF_INET6 && ip6++)
			continue;

		if (path->socket.state == SOCKET_STATE_NONE)
			setup_socket(priv, path);
		else if (path->socket.state == SOCKET_STATE_SCHEDULED && !path->socket.watch) {
			debug_query(priv->query, "socket: resuming connection %d", i);
			path->socket.watch = netresolve_watch_add(priv->query, path->socket.fd, POLLOUT, connection_callback, path);
		}
	}

	if (!ip4 && !ip6)
		error("socket: no connection paths available");
}

static void
pickup_connected_socket(struct netresolve_socket *priv)
{
	struct netresolve_path *paths = priv->query->response.paths;
	struct netresolve_path *found;

	/* Find first scheduled or ready socket. */
	for (found = paths; found->node.family; found++) {
		if (found->socket.state == SOCKET_STATE_SCHEDULED && !priv->skip_scheduled)
			break;
		if (found->socket.state == SOCKET_STATE_READY)
			break;
	}

	/* Pass a ready socket to the application and return. */
	if (found->socket.state == SOCKET_STATE_READY) {
		int fd = found->socket.fd;

		debug_query(priv->query, "socket: passing successful connection %d to the application", found - paths);

		found->socket.state = SOCKET_STATE_DONE;
		found->socket.fd = -1;

		/* Pause all scheduled sockets. */
		for (struct netresolve_path *path = paths; path->node.family; path++) {
			if (path->socket.state == SOCKET_STATE_SCHEDULED) {
				assert(path->socket.watch);

				debug_query(priv->query, "socket: pausing scheduled connection %d", path - paths);

				netresolve_watch_remove(priv->query, path->socket.watch, false);
				path->socket.watch = NULL;
			}
		}

		/* Reset priority timeout. */
		if (priv->priority_timeout) {
			netresolve_timeout_remove(priv->query, priv->priority_timeout);
			priv->priority_timeout = NULL;
			priv->skip_scheduled = false;
		}

		fcntl(fd, F_SETFL, (fcntl(fd, F_GETFL, 0) & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) | priv->flags);
		priv->callback(priv->query, found - paths, fd, priv->user_data);

		return;
	}

	/* Schedule priority timeout if there is a priority socket waiting and
	 * another socket ready and continue.
	 */
	if (found->socket.state == SOCKET_STATE_SCHEDULED) {
		for (struct netresolve_path *path = paths; path->node.family; path++) {
			if (path->socket.state == SOCKET_STATE_READY) {
				if (priv->priority_timeout)
					debug_query(priv->query, "socket: priority timeout already set");
				else {
					debug_query(priv->query, "socket: setting up priority timeout of %d seconds", PRIORITY_TIMEOUT);
					priv->priority_timeout = netresolve_timeout_add(priv->query, PRIORITY_TIMEOUT, 0, timeout_callback, priv);
				}
				break;
			}
		}
	}

	/* Make sure connections are scheduled. */
	enable_sockets(priv);
}

static void
query_callback(netresolve_query_t query, void *user_data)
{
	struct netresolve_socket *priv = user_data;

	debug_query(query, "socket: name resolution done");

	priv->query = query;

	enable_sockets(priv);
}

static void
bind_callback(netresolve_query_t query, void *data)
{
	struct netresolve_socket *priv = data;
	size_t count = netresolve_query_get_count(query);

	for (size_t idx = 0; idx < count; idx++) {
		int flags = O_NONBLOCK;
		int socktype;
		int protocol;
		const struct sockaddr *sa;
		socklen_t salen;
		int sock;

		sa = netresolve_query_get_sockaddr(query, idx, &salen, &socktype, &protocol, NULL);
		if (!sa)
			return;
		sock = socket(sa->sa_family, socktype | flags, protocol);
		if (sock == -1)
			return;
		if (bind(sock, sa, salen) == -1) {
			close(sock);
			return;
		}
		fcntl(sock, F_SETFL, (fcntl(sock, F_GETFL, 0) & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) | priv->flags);

		priv->callback(query, idx, sock, priv->user_data);
	}

	free(priv);
	netresolve_query_free(query);
}

/* netresolve_connect:
 *
 * Perform name resolution and connect to a host. The callback is called
 * once, with the first successfully connected socket. If you want to
 * retry with another address, use `netresolve_connect_next()`. The caller
 * is responsible for closing any sockets received through the callback.
 *
 * You should call `netresolve_connect_free()` once you know you're not
 * going to call `netresolve_connect_free()` or any other API functions
 * on the query to free all resources associated with the connection
 * request.
 */
netresolve_query_t
netresolve_connect(netresolve_t context,
		const char *nodename, const char *servname,
		int family, int socktype, int protocol,
		netresolve_socket_callback_t callback, void *user_data)
{
	int flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC);

	struct netresolve_socket priv = { .callback = callback, .user_data = user_data, .flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC) };

	return netresolve_query(context, query_callback, memdup(&priv, sizeof priv),
			NETRESOLVE_REQUEST_FORWARD,
			NETRESOLVE_OPTION_NODE_NAME, nodename,
			NETRESOLVE_OPTION_SERVICE_NAME, servname,
			NETRESOLVE_OPTION_FAMILY, family,
			NETRESOLVE_OPTION_SOCKTYPE, socktype & ~flags,
			NETRESOLVE_OPTION_PROTOCOL, protocol,
			NETRESOLVE_OPTION_DEFAULT_LOOPBACK, true,
			NULL);
}

/* netresolve_connect_next:
 *
 * When multiple addresses have been found for the target, retry connection
 * with the next available address.
 */
void
netresolve_connect_next(netresolve_query_t query)
{
	struct netresolve_socket *priv = query->user_data;

	enable_sockets(priv);
}

/* netresolve_connect_free:
 *
 * Destroys the query object and releases any related resources.
 */
void
netresolve_connect_free(netresolve_query_t query)
{
	struct netresolve_socket *priv = query->user_data;

	debug("socket: cleaning up...");

	for (int i = 0; i < query->response.pathcount; i++) {
		struct netresolve_path *path = &query->response.paths[i];

		switch (path->socket.state) {
		case SOCKET_STATE_SCHEDULED:
			if (path->socket.watch)
				netresolve_watch_remove(query, path->socket.watch, true);
			break;
		case SOCKET_STATE_READY:
			close(path->socket.fd);
			break;
		default:
			break;
		}

		memset(&path->socket, 0, sizeof path->socket);
	}

	if (priv->priority_timeout)
		netresolve_timeout_remove(query, priv->priority_timeout);

	memset(priv, 0, sizeof *priv);
	free(priv);

	netresolve_query_free(query);
}

/* netresolve_bind:
 *
 * Perform name resolution, bind to all discovered addresses. Resulting
 * file descriptors are passed to the caller via a series of calls to
 * the provided callback.
 */
netresolve_query_t
netresolve_bind(netresolve_t context,
		const char *nodename, const char *servname,
		int family, int socktype, int protocol,
		netresolve_socket_callback_t callback, void *user_data)
{
	struct netresolve_socket priv = { .callback = callback, .user_data = user_data, .flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC) };

	return netresolve_query(context, bind_callback, memdup(&priv, sizeof priv),
			NETRESOLVE_REQUEST_FORWARD,
			NETRESOLVE_OPTION_NODE_NAME, nodename,
			NETRESOLVE_OPTION_SERVICE_NAME, servname,
			NETRESOLVE_OPTION_FAMILY, family,
			NETRESOLVE_OPTION_SOCKTYPE, socktype & ~priv.flags,
			NETRESOLVE_OPTION_PROTOCOL, protocol,
			NETRESOLVE_OPTION_DEFAULT_LOOPBACK, false,
			NULL);
}

/* netresolve_bind_free:
 *
 * Destroys the query object and releases any related resources.
 */
void
netresolve_bind_free(netresolve_query_t query)
{
	netresolve_query_free(query);
}
