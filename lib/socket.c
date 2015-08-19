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
#include <unistd.h>

#include "netresolve-private.h"

#define FIRST_CONNECT_TIMEOUT 1

struct netresolve_socket {
	netresolve_query_t query;
	netresolve_socket_callback_t callback;
	void *user_data;
	int flags;
	netresolve_timeout_t first_connect_timeout;
};

static void
do_connect(netresolve_query_t query, size_t idx)
{
	static const int flags = O_NONBLOCK;
	int socktype;
	int protocol;
	const struct sockaddr *sa;
	socklen_t salen;
	struct netresolve_path *path = &query->response.paths[idx];

	if (path->socket.state != NETRESOLVE_STATE_NONE)
		return;

	sa = netresolve_query_get_sockaddr(query, path - query->response.paths, &salen, &socktype, &protocol, NULL);
	if (!sa)
		goto fail;
	path->socket.fd = socket(sa->sa_family, socktype | flags, protocol);
	if (path->socket.fd == -1)
		goto fail;
	if (connect(path->socket.fd, sa, salen) == -1 && errno != EINPROGRESS)
		goto fail_connect;

	path->socket.watch = netresolve_watch_add(query, path->socket.fd, POLLOUT, NULL);
	path->socket.state = NETRESOLVE_STATE_WAITING;
	return;

fail_connect:
	close(path->socket.fd);
	path->socket.fd = -1;
fail:
	path->socket.state = NETRESOLVE_STATE_FAILED;
}

static void
connect_callback(netresolve_query_t query, void *user_data)
{
	struct netresolve_socket *data = user_data;
	bool ip4 = false;
	bool ip6 = false;
	int i;

	debug_query(query, "socket: connecting");

	data->query = query;
	/* `data` is already stored in `query->user_data`. */

	for (i = 0; i < query->response.pathcount; i++) {
		struct netresolve_path *path = &query->response.paths[i];

		if (!ip4 && path->node.family == AF_INET && path->socket.state == NETRESOLVE_STATE_NONE) {
			do_connect(query, path - query->response.paths);
			ip4 = true;
		}
		if (!ip6 && path->node.family == AF_INET6 && path->socket.state == NETRESOLVE_STATE_NONE) {
			do_connect(query, path - query->response.paths);
			ip6 = true;
		}
	}

}

static void
bind_callback(netresolve_query_t query, void *user_data)
{
	struct netresolve_socket *data = user_data;
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
		fcntl(sock, F_SETFL, (fcntl(sock, F_GETFL, 0) & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) | data->flags);

		data->callback(query, idx, sock, data->user_data);
	}

	free(data);
	netresolve_query_free(query);
}

static void *
memdup(const void *source, size_t len)
{
	void *target = malloc(len);

	if (target)
		memcpy(target, source, len);

	return target;
}

static void
do_cleanup(struct netresolve_socket *data)
{
	netresolve_query_t query = data->query;
	int i;

	debug("socket: cleaning up...");

	for (i = 0; i < query->response.pathcount; i++) {
		struct netresolve_path *path = &query->response.paths[i];

		switch (path->socket.state) {
		case NETRESOLVE_STATE_WAITING:
		case NETRESOLVE_STATE_DONE:
			netresolve_watch_remove(query, path->socket.watch, true);
			/* pass through */
		default:
			memset(&path->socket, 0, sizeof path->socket);
		}
	}

	if (data->first_connect_timeout) {
		netresolve_timeout_remove(query, data->first_connect_timeout);
		data->first_connect_timeout = NULL;
	}

	free(data);
	netresolve_query_free(query);
}

static void
connect_check(struct netresolve_socket *data)
{
	netresolve_query_t query = data->query;
	int idx;

	for (idx = 0; idx < query->response.pathcount; idx++) {
		struct netresolve_path *path = &query->response.paths[idx];

		if (path->socket.state < NETRESOLVE_STATE_DONE)
			break;

		if (path->socket.state == NETRESOLVE_STATE_DONE) {
			int sock = path->socket.fd;

			netresolve_watch_remove(query, path->socket.watch, false);
			fcntl(sock, F_SETFL, (fcntl(sock, F_GETFL, 0) & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) | data->flags);
			data->callback(query, idx, sock, data->user_data);
			path->socket.state = NETRESOLVE_STATE_NONE;
			do_cleanup(data);
			break;
		}
	}
}

static void
connect_finished(struct netresolve_socket *data, struct netresolve_path *path)
{
	netresolve_query_t query = data->query;

	path->socket.state = NETRESOLVE_STATE_DONE;

	if (!data->first_connect_timeout)
		data->first_connect_timeout = netresolve_timeout_add(query, FIRST_CONNECT_TIMEOUT, 0, NULL);

	connect_check(data);
}

static void
connect_failed(struct netresolve_socket *data, struct netresolve_path *path)
{
	netresolve_query_t query = data->query;
	int family = path->node.family;

	path->socket.state = NETRESOLVE_STATE_FAILED;
	close(path->socket.fd);

	while (path < query->response.paths + query->response.pathcount)
		if (path->node.family == family)
			do_connect(query, path - query->response.paths);

	connect_check(data);
}

netresolve_query_t
netresolve_connect(netresolve_t context,
		const char *nodename, const char *servname,
		int family, int socktype, int protocol,
		netresolve_socket_callback_t callback, void *user_data)
{
	int flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC);

	struct netresolve_socket data = { .callback = callback, .user_data = user_data, .flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC) };

	return netresolve_query(context, connect_callback, memdup(&data, sizeof data),
			NETRESOLVE_REQUEST_FORWARD,
			NETRESOLVE_OPTION_NODE_NAME, nodename,
			NETRESOLVE_OPTION_SERVICE_NAME, servname,
			NETRESOLVE_OPTION_FAMILY, family,
			NETRESOLVE_OPTION_SOCKTYPE, socktype & ~flags,
			NETRESOLVE_OPTION_PROTOCOL, protocol,
			NETRESOLVE_OPTION_DEFAULT_LOOPBACK, true,
			NULL);
}

netresolve_query_t
netresolve_bind(netresolve_t context,
		const char *nodename, const char *servname,
		int family, int socktype, int protocol,
		netresolve_socket_callback_t callback, void *user_data)
{
	struct netresolve_socket data = { .callback = callback, .user_data = user_data, .flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC) };

	return netresolve_query(context, bind_callback, memdup(&data, sizeof data),
			NETRESOLVE_REQUEST_FORWARD,
			NETRESOLVE_OPTION_NODE_NAME, nodename,
			NETRESOLVE_OPTION_SERVICE_NAME, servname,
			NETRESOLVE_OPTION_FAMILY, family,
			NETRESOLVE_OPTION_SOCKTYPE, socktype & ~data.flags,
			NETRESOLVE_OPTION_PROTOCOL, protocol,
			NETRESOLVE_OPTION_DEFAULT_LOOPBACK, false,
			NULL);
}

bool
netresolve_connect_dispatch(netresolve_query_t query, int fd, int events)
{
	struct netresolve_socket *data = query->user_data;
	int i;

	debug("socket: dispatching file descriptor: %d %d", fd, events);

	for (i = 0; i < query->response.pathcount; i++) {
		struct netresolve_path *path = &query->response.paths[i];

		if (fd == path->socket.fd) {
			assert(events & POLLOUT);

			socklen_t len = sizeof(errno);
			getsockopt(path->socket.fd, SOL_SOCKET, SO_ERROR, &errno, &len);

			if (errno)
				connect_failed(data, path);
			else
				connect_finished(data, path);

			return true;
		}
	}

	if (fd == data->first_connect_timeout->fd) {
		for (i = 0; i < query->response.pathcount; i++) {
			struct netresolve_path *path = &query->response.paths[i];

			switch (path->socket.state) {
			case NETRESOLVE_STATE_WAITING:
				netresolve_watch_remove(query, path->socket.watch, true);
				/* pass through */
			case NETRESOLVE_STATE_NONE:
				path->socket.state = NETRESOLVE_STATE_FAILED;
				break;
			default:
				break;
			}
		}

		connect_check(data);
		return true;
	}

	return false;
}
