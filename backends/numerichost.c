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
#include <netresolve-backend.h>

void
start(netresolve_backend_t resolver, char **settings)
{
	const char *request_node = netresolve_backend_get_node(resolver);
	Address address;
	int family;
	int ifindex;

	if (!netresolve_backend_parse_address(request_node, &address, &family, &ifindex)) {
		netresolve_backend_failed(resolver);
		return;
	}

	netresolve_backend_add_address(resolver, family, &address, ifindex);
	netresolve_backend_finished(resolver);
}
