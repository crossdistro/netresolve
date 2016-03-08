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

struct netresolve_socket {
	netresolve_query_t query;
	netresolve_socket_callback_t callback;
	void *user_data;
	int flags;
	netresolve_timeout_t timeout;
	netresolve_timeout_t priority_timeout;
	bool skip_scheduled;
	bool sequential_connect;
};

static void
clear_timeouts(struct netresolve_socket *priv)
{
	if (priv->timeout) {
		netresolve_timeout_remove(priv->query, priv->timeout);
		priv->timeout = NULL;
	}
	if (priv->priority_timeout) {
		netresolve_timeout_remove(priv->query, priv->priority_timeout);
		priv->priority_timeout = NULL;
	}
}

static void
socket_schedule(struct netresolve_socket *priv, struct netresolve_path *path, int events, netresolve_watch_callback_t callback)
{
	assert(path->socket.state == SOCKET_STATE_NONE);

	path->socket.state = SOCKET_STATE_SCHEDULED;
	path->socket.watch = netresolve_watch_add(priv->query, path->socket.fd, events, callback, path);
}

static void
socket_pause(struct netresolve_socket *priv, struct netresolve_path *path)
{
	assert(path->socket.state == SOCKET_STATE_SCHEDULED);
	assert(path->socket.watch);

	debug_query(priv->query, "socket: pausing scheduled connection %d", path - priv->query->response.paths);

	netresolve_watch_remove(priv->query, path->socket.watch, false);
	path->socket.watch = NULL;
}

static void
socket_resume(struct netresolve_socket *priv, struct netresolve_path *path, int events, netresolve_watch_callback_t callback)
{
	assert(path->socket.state == SOCKET_STATE_SCHEDULED);
	assert(!path->socket.watch);

	path->socket.watch = netresolve_watch_add(priv->query, path->socket.fd, events, callback, path);
}

static void
socket_cleanup(struct netresolve_socket *priv, struct netresolve_path *path)
{
	path->socket.state = SOCKET_STATE_DONE;
	if (path->socket.watch) {
		netresolve_watch_remove(priv->query, path->socket.watch, false);
		path->socket.watch = NULL;
	}
	if (path->socket.fd != -1) {
		close(path->socket.fd);
		path->socket.fd = -1;
	}
}

static void pickup_connected_socket(struct netresolve_socket *priv);
static void enable_sockets(struct netresolve_socket *priv);
static void connect_ready(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data);

static void
connect_check(struct netresolve_socket *priv, struct netresolve_path *path)
{
	int idx = path - priv->query->response.paths;
	const char *family = path->node.family == AF_INET ? "IPv4" : "IPv6";

	switch (errno) {
	case EINPROGRESS:
		debug_query(priv->query, "socket: connection %d via %s scheduled", idx, family);
		socket_schedule(priv, path, POLLOUT, connect_ready);
		return;
	case 0:
		debug_query(priv->query, "socket: connection %d via %s succeeded", idx, family);
		if (path->socket.state == SOCKET_STATE_SCHEDULED)
			socket_pause(priv, path);
		path->socket.state = SOCKET_STATE_READY;
		break;
	default:
		error("socket: connection %d via %s failed: %s", idx, family, strerror(errno));
		socket_cleanup(priv, path);
	}

	/* See whether we can pass back a ready connection. */
	pickup_connected_socket(priv);
}

static void
connect_ready(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data)
{
	struct netresolve_socket *priv = query->user_data;
	struct netresolve_path *path = data;
	socklen_t len = sizeof(errno);

	assert(fd == path->socket.fd);
	assert(events == POLLOUT);
	assert(path->socket.state == SOCKET_STATE_SCHEDULED);
	assert(path->socket.watch);

	/* Check result of non-blocking `connect()`. */
	getsockopt(fd, SOL_SOCKET, SO_ERROR, &errno, &len);
	connect_check(priv, path);
}

static void
connect_start(struct netresolve_socket *priv, struct netresolve_path *path)
{
	struct netresolve_path *paths = priv->query->response.paths;
	int socktype;
	int protocol;
	const struct sockaddr *sa;
	socklen_t salen;

	assert(path->socket.state == SOCKET_STATE_NONE);

	if (!(sa = netresolve_query_get_sockaddr(priv->query, path - paths, &salen, &socktype, &protocol, NULL))) {
		error("socket: cannot get socket address");
		return;
	}

	/* Attempt a non-blocking connect. */
	errno = 0;
	path->socket.fd = socket(sa->sa_family, socktype | O_NONBLOCK, protocol);
	if (!errno)
		connect(path->socket.fd, sa, salen);
	connect_check(priv, path);
}

static void
connect_timeout(netresolve_query_t query, netresolve_timeout_t timeout, void *data)
{
	struct netresolve_socket *priv = data;
	struct netresolve_path *paths = query->response.paths;

	debug_query(query, "socket: connection timeout occured");

	clear_timeouts(priv);

	/* Kill all waited connections. */
	for (struct netresolve_path *path = paths; path->node.family; path++)
		if (path->socket.state == SOCKET_STATE_SCHEDULED)
			socket_cleanup(priv, path);

	enable_sockets(priv);
}

static void
connect_priority_timeout(netresolve_query_t query, netresolve_timeout_t timeout, void *data)
{
	struct netresolve_socket *priv = data;

	debug_query(query, "socket: connection priority timeout occured");

	priv->skip_scheduled = true;

	pickup_connected_socket(priv);
}

static void
enable_sockets(struct netresolve_socket *priv)
{
	netresolve_query_t query = priv->query;
	struct netresolve_path *paths = query->response.paths;
	int ip4 = 0, ip6 = 0;
	int timeout = getenv_int("NETRESOLVE_CONNECT_TIMEOUT", 15);

	/* Attempt to connect to one IPv4 and one IPv6 address in parallel. It is a
	 * „happy eyeballs“ style optimization that removes a delay when packets
	 * of the preferred protocol (typically IPvy) are dropped on the way which
	 * is a very common network misconfiguration.
	 */
	for (struct netresolve_path *path = paths; path->node.family; path++) {
		/* Skip already attempted sockets. */
		if (path->socket.state == SOCKET_STATE_DONE)
			continue;

		/* Skip all but first address for each protocol. */
		if (path->node.family == AF_INET && ip4++)
			continue;
		if (path->node.family == AF_INET6 && ip6++)
			continue;

		/* Will start or resume connection process, set up the connection timeout. */
		if (!priv->timeout)
			priv->timeout = netresolve_timeout_add(priv->query, timeout, 0, connect_timeout, priv);

		if (path->socket.state == SOCKET_STATE_NONE)
			connect_start(priv, path);
		else if (path->socket.state == SOCKET_STATE_SCHEDULED && !path->socket.watch) {
			debug_query(priv->query, "socket: resuming connection %d", path - paths);
			socket_resume(priv, path, POLLOUT, connect_ready);
		}

		/* When in sequential mode, keep only one socket scheduled. */
		if (priv->sequential_connect)
			break;
	}

	if (!ip4 && !ip6) {
		error("socket: no connection paths available");
		clear_timeouts(priv);
	}
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
		for (struct netresolve_path *path = paths; path->node.family; path++)
			if (path->socket.state == SOCKET_STATE_SCHEDULED)
				socket_pause(priv, path);

		/* Reset timeouts. */
		if (priv->priority_timeout)
			priv->skip_scheduled = false;
		clear_timeouts(priv);

		fcntl(fd, F_SETFL, (fcntl(fd, F_GETFL, 0) & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) | priv->flags);
		priv->callback(priv->query, found - paths, fd, priv->user_data);

		return;
	}

	/* Schedule priority timeout if there is a priority socket waiting and
	 * another socket ready and continue.
	 */
	if (found->socket.state == SOCKET_STATE_SCHEDULED) {
		for (found = paths; found->node.family; found++)
			if (found->socket.state == SOCKET_STATE_READY)
				break;
		if (found->socket.state == SOCKET_STATE_READY) {
			if (priv->priority_timeout)
				debug_query(priv->query, "socket: priority timeout already set");
			else {
				int priority_timeout = getenv_int("NETRESOLVE_CONNECT_PRIORITY_TIMEOUT", 1);

				debug_query(priv->query, "socket: setting up priority timeout of %d seconds", priority_timeout);
				priv->priority_timeout = netresolve_timeout_add(priv->query, priority_timeout, 0, connect_priority_timeout, priv);
			}
		}
	}

	/* Make sure connections are scheduled. */
	enable_sockets(priv);
}

static void
connect_prepare(netresolve_query_t query, void *user_data)
{
	struct netresolve_socket *priv = user_data;

	debug_query(query, "socket: name resolution done, will attempt to connect");

	priv->query = query;

	enable_sockets(priv);
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
	struct netresolve_socket priv = {
		.callback = callback,
		.user_data = user_data,
		.flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC),
		.sequential_connect = getenv_bool("NETRESOLVE_SEQUENTIAL_CONNECT", false)
	};

	return netresolve_query(context, connect_prepare, memdup(&priv, sizeof priv),
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
	struct netresolve_path *paths = priv->query->response.paths;

	debug("socket: cleaning up...");

	for (struct netresolve_path *path = paths; path->node.family; path++)
		socket_cleanup(priv, path);

	clear_timeouts(priv);

	memset(priv, 0, sizeof *priv);
	//free(priv);

	netresolve_query_free(query);
}

static void
listen_callback(netresolve_query_t query, void *user_data)
{
	struct netresolve_socket *priv = user_data;
	struct netresolve_path *paths = query->response.paths;

	debug_query(query, "socket: name resolution done, will attempt to listen");

	priv->query = query;

	for (struct netresolve_path *path = paths; path->node.family; path++) {
		int socktype;
		int protocol;
		const struct sockaddr *sa;
		socklen_t salen;
		static const socklen_t one = 1;

		if (!(sa = netresolve_query_get_sockaddr(query, path - paths, &salen, &socktype, &protocol, NULL)))
			continue;
		if ((path->socket.fd = socket(sa->sa_family, socktype | O_NONBLOCK, protocol)) == -1)
			continue;
		if (setsockopt(path->socket.fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) == -1)
			continue;
		if (sa->sa_family == AF_INET6 && setsockopt(path->socket.fd, SOL_IPV6, IPV6_V6ONLY, &one, sizeof one) == -1)
			continue;
		if (bind(path->socket.fd, sa, salen) == -1 || listen(path->socket.fd, SOMAXCONN) == -1) {
			close(path->socket.fd);
			continue;
		}

		path->socket.state = SOCKET_STATE_SCHEDULED;
	}
}

static void
accept_callback(netresolve_query_t query, netresolve_watch_t watch, int event_fd, int events, void *data)
{
	struct netresolve_socket *priv = query->user_data;
	struct netresolve_path *path = data;
	int fd;
	int flags;

	assert(event_fd == path->socket.fd);
	assert(events & POLLIN);
	assert(path->socket.state == SOCKET_STATE_SCHEDULED);
	assert(path->socket.watch);

	if ((fd = accept(event_fd, NULL, NULL)) == -1)
		goto fail;
	if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
		goto fail;
	flags &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
	flags |= priv->flags;
	if ((fcntl(fd, F_SETFL, flags)) == -1)
		goto fail;

	if (priv->callback)
		priv->callback(query, path - query->response.paths, fd, priv->user_data);

	return;
fail:
	error("Error accepting connection: %s", strerror(errno));
}

/* netresolve_listen:
 *
 * Perform name resolution, bind to all discovered addresses and start
 * listening. You need to call `netresolve_accept()` to start accepting
 * connections.
 */
netresolve_query_t
netresolve_listen(netresolve_t context,
		const char *nodename, const char *servname,
		int family, int socktype, int protocol)
{
	struct netresolve_socket priv = { .flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC) };

	return netresolve_query(context, listen_callback, memdup(&priv, sizeof priv),
			NETRESOLVE_REQUEST_FORWARD,
			NETRESOLVE_OPTION_NODE_NAME, nodename,
			NETRESOLVE_OPTION_SERVICE_NAME, servname,
			NETRESOLVE_OPTION_FAMILY, family,
			NETRESOLVE_OPTION_SOCKTYPE, socktype & ~priv.flags,
			NETRESOLVE_OPTION_PROTOCOL, protocol,
			NETRESOLVE_OPTION_DEFAULT_LOOPBACK, false,
			NULL);
}

/* netresolve_accept:
 *
 * Start accepting connections. You need to call `netresolve_listen_free()` to
 * close listening sockets and release memory.
 */
void
netresolve_accept(netresolve_query_t query, netresolve_socket_callback_t on_accept, void *user_data)
{
	struct netresolve_socket *priv = query->user_data;
	struct netresolve_path *paths = query->response.paths;

	priv->callback = on_accept;
	priv->user_data = user_data;

	for (struct netresolve_path *path = paths; path->node.family; path++)
		if (path->socket.state == SOCKET_STATE_SCHEDULED && !path->socket.watch)
			socket_resume(priv, path, POLLIN, accept_callback);

	/* Wait for the context in blocking mode. */
	if (query->context->callbacks.user_data == &query->context->epoll)
		netresolve_epoll_wait(query->context);
}

/* netresolve_listen_free:
 *
 * Destroys the query object and releases any related resources.
 */
void
netresolve_listen_free(netresolve_query_t query)
{
	struct netresolve_socket *priv = query->user_data;
	struct netresolve_path *paths = query->response.paths;

	debug("socket: cleaning up...");

	for (struct netresolve_path *path = paths; path->node.family; path++)
		socket_cleanup(priv, path);

	memset(priv, 0, sizeof *priv);
	free(priv);

	netresolve_query_free(query);
}
