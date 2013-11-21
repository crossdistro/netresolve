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
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "netresolve-private.h"

static const char *
socktype_to_string(int socktype)
{
	switch (socktype) {
	case 0:
		return "any";
	case SOCK_DGRAM:
		return "dgram";
	case SOCK_STREAM:
		return "stream";
	case SOCK_SEQPACKET:
		return "seqpacket";
	case SOCK_RAW:
		return "raw";
	default:
		return "unknown";
	}
}

static const char *
protocol_to_string(int proto)
{
	switch (proto) {
	case 0:
		return "any";
	case IPPROTO_UDP:
		return "udp";
	case IPPROTO_TCP:
		return "tcp";
	case IPPROTO_SCTP:
		return "sctp";
	default:
		return "unknown";
	}
}

static int
bprintf(char **current, char *end, const char *fmt, ...)
{
	va_list ap;
	ssize_t size;

	va_start(ap, fmt);
	size = vsnprintf(*current, end - *current, fmt, ap);
	if (size > 0)
		(*current) += size;
	va_end(ap);

	return size;
}

static void
add_path(char **start, char *end, netresolve_t resolver, int i)
{
	int family, ifindex, socktype, protocol, port, priority, weight;
	const void *address;
	char ifname[IF_NAMESIZE] = {0};
	char addrstr[1024] = {0};
	const char *socktypestr;
	const char *protocolstr;

	netresolve_get_path(resolver, i,
			&family, &address, &ifindex,
			&socktype, &protocol, &port,
			&priority, &weight);

	socktypestr = socktype_to_string(socktype);
	protocolstr = protocol_to_string(protocol);

	if (family == AF_INET || family == AF_INET6)
		inet_ntop(family, address, addrstr, sizeof addrstr);

	if (family == AF_UNIX)
		bprintf(start, end, "unix %s %s\n", address, socktypestr);
	else if (ifindex) {
		if (!if_indextoname(ifindex, ifname))
			snprintf(ifname, sizeof ifname, "%d", ifindex);
		bprintf(start, end, "path %s%%%s %s %s %d %d %d\n",
				addrstr, ifname, socktypestr, protocolstr, port, priority, weight);
	} else {
		bprintf(start, end, "path %s %s %s %d %d %d\n",
				addrstr, socktypestr, protocolstr, port, priority, weight);
	}
}

const char *
netresolve_get_request_string(netresolve_t resolver)
{
	const char *node = netresolve_backend_get_node(resolver);
	const char *service = netresolve_backend_get_service(resolver);
	char *start = resolver->buffer;
	char *end = resolver->buffer + sizeof resolver->buffer;

	bprintf(&start, end, "request %s %s\n", PACKAGE_NAME, VERSION);
	if (node)
		bprintf(&start, end, "node %s\n", node);
	if (service)
		bprintf(&start, end, "service %s\n", service);
	bprintf(&start, end, "\n");

	return resolver->buffer;
}

const char *
netresolve_get_path_string(netresolve_t resolver, int i)
{
	char *start = resolver->buffer;
	char *end = resolver->buffer + sizeof resolver->buffer;

	add_path(&start, end, resolver, i);

	return resolver->buffer;
}

const char *
netresolve_get_response_string(netresolve_t resolver)
{
	char *start = resolver->buffer;
	char *end = resolver->buffer + sizeof resolver->buffer;

	size_t npaths = netresolve_get_path_count(resolver);
	size_t i;

	bprintf(&start, end, "response %s %s\n", PACKAGE_NAME, VERSION);
	for (i = 0; i < npaths; i++)
		add_path(&start, end, resolver, i);
	bprintf(&start, end, "\n");

	return resolver->buffer;
}
