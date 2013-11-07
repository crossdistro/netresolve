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
#include <fcntl.h>

#include <netresolve.h>
#include <netresolve-utils.h>

static void
on_socket(netresolve_t resolver, int sock, void *user_data)
{
	int *psock = user_data;

	if (*psock == -1)
		*psock = sock;
	else
		close(sock);
}

static void
set_flags(int sock, int flags)
{
	fcntl(sock, F_SETFL, (fcntl(sock, F_GETFL, 0) & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) | flags);
}

int
netresolve_utils_bind(const char *node, const char *service, int family, int socktype, int protocol)
{
	netresolve_t resolver = netresolve_open();
	int sock = -1;
	int flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC);
	int status;

	if (!resolver)
		return -1;

	netresolve_callback_set_bind(resolver, on_socket, &sock);

	socktype &= ~flags;

	status = netresolve_resolve(resolver, node, service, family, socktype, protocol);

	netresolve_close(resolver);

	if (status)
		errno = status;
	return sock;
}

int
netresolve_utils_connect(const char *node, const char *service, int family, int socktype, int protocol)
{
	netresolve_t resolver;
	int sock = -1;
	int flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC);
	int status;

	socktype &= ~flags;

	resolver = netresolve_open();
	if (!resolver)
		return -1;
	netresolve_callback_set_connect(resolver, on_socket, &sock);
	status = netresolve_resolve(resolver, node, service, family, socktype, protocol);
	netresolve_close(resolver);

	if (sock != -1)
		set_flags(sock, flags);

	if (status)
		errno = status;
	return sock;
}
