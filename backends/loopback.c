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
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>

#include <netresolve-backend.h>

#if BYTE_ORDER == BIG_ENDIAN
static const struct in_addr inaddr_loopback = { 0x7f000001 };
#elif BYTE_ORDER == LITTLE_ENDIAN
static const struct in_addr inaddr_loopback = { 0x0100007f };
#else
	#error Neither big endian nor little endian
#endif

void
start(netresolve_backend_t resolver, char **settings)
{
	const char *node = netresolve_backend_get_node(resolver);
	bool ipv4 = !node || !*node || !strcmp(node, "localhost") || !strcmp(node, "localhost4");
	bool ipv6 = !node || !*node || !strcmp(node, "localhost") || !strcmp(node, "localhost6");

	if (!ipv4 && !ipv6) {
		netresolve_backend_failed(resolver);
		return;
	}

	if (ipv4)
		netresolve_backend_add_address(resolver, AF_INET, &inaddr_loopback, 0);
	if (ipv6)
		netresolve_backend_add_address(resolver, AF_INET6, &in6addr_loopback, 0);
	netresolve_backend_finished(resolver);
}
