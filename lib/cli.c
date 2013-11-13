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
#include <netresolve.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int
family_from_string(const char *str)
{
	if (!strcmp(str, "ip4"))
		return AF_INET;
	if (!strcmp(str, "ip6"))
		return AF_INET6;
	if (!strcmp(str, "unix"))
		return AF_UNIX;
	/* "any" */
	return AF_UNSPEC;
}

int
socktype_from_string(const char *str)
{
	if (!strcmp(str, "stream"))
		return SOCK_STREAM;
	if (!strcmp(str, "dgram"))
		return SOCK_DGRAM;
	if (!strcmp(str, "seqpacket"))
		return SOCK_SEQPACKET;
	return 0;
}

int
protocol_from_string(const char *str)
{
	if (!strcmp(str, "tcp"))
		return IPPROTO_TCP;
	if (!strcmp(str, "udp"))
		return IPPROTO_UDP;
	if (!strcmp(str, "sctp"))
		return IPPROTO_SCTP;
	return 0;
}

int
netresolve_resolve_argv(netresolve_t resolver, char **argv)
{
	const char *node = NULL, *service = NULL;
	int family = 0, socktype = 0, protocol = 0;

	if (*argv) {
		node = *argv++;
		if (node[0] == '-' && !node[1])
			node = NULL;
	}
	if (*argv) {
		service = *argv++;
		if (service[0] == '-' && !service[1])
			service = NULL;
	} if (*argv)
		family = family_from_string(*argv++);
	if (*argv)
		socktype = socktype_from_string(*argv++);
	if (*argv)
		protocol = protocol_from_string(*argv++);

	return netresolve_resolve(resolver, node, service, family, socktype, protocol);
}
