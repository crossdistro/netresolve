/* This file is part of the `netresolve` library.
 * Copyright (C) Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <unistd.h>

#include "netresolve-private.h"

void
_netresolve_bind_path(netresolve_t resolver, struct netresolve_path *path)
{
	int flags = O_NONBLOCK;
	int socktype;
	int protocol;
	const struct sockaddr *sa;
	socklen_t salen;
	int sock;

	sa = netresolve_get_path_sockaddr(resolver, path - resolver->response.paths, &socktype, &protocol, &salen);
	if (!sa)
		return;
	sock = socket(sa->sa_family, socktype | flags, protocol);
	if (sock == -1)
		return;
	if (bind(sock, sa, salen) == -1) {
		close(sock);
		return;
	}

	resolver->callbacks.on_bind(resolver, sock, resolver->callbacks.user_data_sock);
}

static void
connect_path(netresolve_t resolver, struct netresolve_path *path)
{
	static const int flags = O_NONBLOCK;
	int socktype;
	int protocol;
	const struct sockaddr *sa;
	socklen_t salen;

	if (path->socket.state != NETRESOLVE_STATE_INIT)
		return;

	sa = netresolve_get_path_sockaddr(resolver, path - resolver->response.paths, &socktype, &protocol, &salen);
	if (!sa)
		goto fail;
	path->socket.fd = socket(sa->sa_family, socktype | flags, protocol);
	if (path->socket.fd == -1)
		goto fail;
	if (connect(path->socket.fd, sa, salen) == -1 && errno != EINPROGRESS)
		goto fail_connect;

	_netresolve_watch_fd(resolver, path->socket.fd, POLLOUT);
	path->socket.state = NETRESOLVE_STATE_WAITING;
	return;

fail_connect:
	close(path->socket.fd);
	path->socket.fd = -1;
fail:
	path->socket.state = NETRESOLVE_STATE_FAILED;
}

static void
connect_check(netresolve_t resolver)
{
	int i;

	for (i = 0; i < resolver->response.pathcount; i++) {
		struct netresolve_path *path = &resolver->response.paths[i];

		if (path->socket.state < NETRESOLVE_STATE_FINISHED)
			break;

		if (path->socket.state == NETRESOLVE_STATE_FINISHED) {
			resolver->callbacks.on_connect(resolver, path->socket.fd, resolver->callbacks.user_data_sock);
			path->socket.state = NETRESOLVE_STATE_INIT;
			_netresolve_set_state(resolver, NETRESOLVE_STATE_FINISHED);
			break;
		}
	}
}

static void
connect_finished(netresolve_t resolver, struct netresolve_path *path)
{
	path->socket.state = NETRESOLVE_STATE_FINISHED;

	if (resolver->first_connect_timeout == -1)
		resolver->first_connect_timeout = _netresolve_add_timeout(resolver, 1, 0);

	connect_check(resolver);
}

static void
connect_failed(netresolve_t resolver, struct netresolve_path *path)
{
	int family = path->node.family;

	path->socket.state = NETRESOLVE_STATE_FAILED;
	close(path->socket.fd);

	while (path < resolver->response.paths + resolver->response.pathcount)
		if (path->node.family == family)
			connect_path(resolver, path);

	connect_check(resolver);
}

void
_netresolve_connect_start(netresolve_t resolver)
{
	bool ip4 = false;
	bool ip6 = false;
	int i;

	debug("socket: connecting...\n");

	for (i = 0; i < resolver->response.pathcount; i++) {
		struct netresolve_path *path = &resolver->response.paths[i];

		if (!ip4 && path->node.family == AF_INET && path->socket.state == NETRESOLVE_STATE_INIT) {
			connect_path(resolver, path);
			ip4 = true;
		}
		if (!ip6 && path->node.family == AF_INET6 && path->socket.state == NETRESOLVE_STATE_INIT) {
			connect_path(resolver, path);
			ip6 = true;
		}
	}
}

bool
_netresolve_connect_dispatch(netresolve_t resolver, int fd, int events)
{
	int i;

	debug("socket: dispatching file descriptor: %d %d\n", fd, events);

	for (i = 0; i < resolver->response.pathcount; i++) {
		struct netresolve_path *path = &resolver->response.paths[i];

		if (fd == path->socket.fd) {
			_netresolve_watch_fd(resolver, path->socket.fd, 0);

			if (events & POLLOUT) {
				socklen_t len = sizeof(errno);

				getsockopt(path->socket.fd, SOL_SOCKET, SO_ERROR, &errno, &len);

				if (errno)
					connect_failed(resolver, path);
				else
					connect_finished(resolver, path);
			}

			return true;
		}
	}

	if (fd == resolver->first_connect_timeout) {
		for (i = 0; i < resolver->response.pathcount; i++) {
			struct netresolve_path *path = &resolver->response.paths[i];

			switch (path->socket.state) {
			case NETRESOLVE_STATE_WAITING:
				close(path->socket.fd);
				/* path through */
			case NETRESOLVE_STATE_INIT:
				path->socket.state = NETRESOLVE_STATE_FAILED;
				break;
			default:
				break;
			}
		}

		connect_check(resolver);
		return true;
	}

	return false;
}

void
_netresolve_connect_cleanup(netresolve_t resolver)
{
	int i;

	debug("socket: cleaning up...\n");

	for (i = 0; i < resolver->response.pathcount; i++) {
		struct netresolve_path *path = &resolver->response.paths[i];

		switch (path->socket.state) {
		case NETRESOLVE_STATE_WAITING:
		case NETRESOLVE_STATE_FINISHED:
			close(path->socket.fd);
			/* pass through */
		default:
			memset(&path->socket, 0, sizeof path->socket);
		}
	}

	if (resolver->first_connect_timeout != -1) {
		_netresolve_remove_timeout(resolver, resolver->first_connect_timeout);
		resolver->first_connect_timeout = -1;
	}
}
