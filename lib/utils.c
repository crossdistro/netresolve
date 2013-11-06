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
#include <errno.h>

#include <netresolve.h>
#include <netresolve-utils.h>

static int
netresolve_bind(netresolve_t resolver, int flags)
{
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
		if (bind(sock, sa, salen) == -1) {
			close(sock);
			continue;
		}

		return sock;
	}

	return -1;
}

static int
netresolve_connect(netresolve_t resolver, int flags)
{
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
		if (connect(sock, sa, salen) == -1) {
			close(sock);
			continue;
		}

		return sock;
	}

	return -1;
}

int
netresolve_utils_bind(const char *node, const char *service, int family, int socktype, int protocol)
{
	netresolve_t resolver = netresolve_open();
	int flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC);
	int status;

	if (!resolver)
		return -1;

	netresolve_unset_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);
	socktype &= ~flags;

	status = netresolve_resolve(resolver, node, service, family, socktype, protocol);
	if (status) {
		errno = status;
		return -1;
	}

	return netresolve_bind(resolver, flags);
}

int
netresolve_utils_connect(const char *node, const char *service, int family, int socktype, int protocol)
{
	netresolve_t resolver = netresolve_open();
	int flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC);
	int status;

	if (!resolver)
		return -1;

	netresolve_set_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);
	socktype &= ~flags;

	status = netresolve_resolve(resolver, node, service, family, socktype, protocol);
	if (status) {
		errno = status;
		return -1;
	}

	return netresolve_connect(resolver, flags);
}
