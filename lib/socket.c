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

void
_netresolve_connect(netresolve_t resolver)
{
	int flags = O_NONBLOCK;
	size_t npaths = netresolve_get_path_count(resolver);
	size_t i;

	for (i = 0; i < npaths; i++) {
		int socktype;
		int protocol;
		const struct sockaddr *sa;
		socklen_t salen;
		int sock;

		sa = netresolve_get_path_sockaddr(resolver, i, &socktype, &protocol, &salen);
		if (!sa)
			continue;
		sock = socket(sa->sa_family, socktype | flags, protocol);
		if (sock == -1)
			continue;
		if (connect(sock, sa, salen) == -1 && errno != EINPROGRESS) {
			close(sock);
			continue;
		}

		resolver->callbacks.on_connect(resolver, sock, resolver->callbacks.user_data_sock);
		return;
	}

	_netresolve_set_state(resolver, NETRESOLVE_STATE_FAILED);
}

