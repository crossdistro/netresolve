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
#include <netresolve-private.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

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

int
netresolve_family_from_string(const char *str)
{
	if (!str)
		return AF_UNSPEC;
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
netresolve_socktype_from_string(const char *str)
{
	if (!strcmp(str, "raw"))
		return SOCK_RAW;
	if (!strcmp(str, "stream"))
		return SOCK_STREAM;
	if (!strcmp(str, "dgram"))
		return SOCK_DGRAM;
	if (!strcmp(str, "seqpacket"))
		return SOCK_SEQPACKET;
	return 0;
}

int
netresolve_protocol_from_string(const char *str)
{
	if (!strcmp(str, "tcp"))
		return IPPROTO_TCP;
	if (!strcmp(str, "udp"))
		return IPPROTO_UDP;
	if (!strcmp(str, "sctp"))
		return IPPROTO_SCTP;
	return 0;
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
add_path(char **start, char *end, netresolve_query_t query, int i)
{
	int family, ifindex, socktype, protocol, port, priority, weight, ttl;
	const void *address;
	char ifname[IF_NAMESIZE+1+1] = {0};
	char addrstr[1024] = {0};
	const char *socktypestr;
	const char *protocolstr;

	netresolve_query_get_node_info(query, i, &family, &address, &ifindex);
	netresolve_query_get_service_info(query, i, &socktype, &protocol, &port);
	netresolve_query_get_aux_info(query, i, &priority, &weight, &ttl);

	socktypestr = socktype_to_string(socktype);
	protocolstr = protocol_to_string(protocol);

	if (family == AF_INET || family == AF_INET6)
		inet_ntop(family, address, addrstr, sizeof addrstr);

	if (family == AF_UNIX)
		bprintf(start, end, "unix %s %s", address, socktypestr);
	else {
		if (ifindex) {
			ifname[0] = '%';
			if (!if_indextoname(ifindex, ifname + 1))
				snprintf(ifname + 1, sizeof ifname - 1, "%d", ifindex);
		}
		bprintf(start, end, "ip %s%s %s %s %d %d %d %d",
				addrstr, ifname,
				socktypestr, protocolstr, port,
				priority, weight, ttl);
	}
}

const char *
netresolve_get_request_string(netresolve_query_t query)
{
	const char *node = netresolve_backend_get_nodename(query);
	const char *service = netresolve_backend_get_servname(query);
	char *start = query->buffer;
	char *end = query->buffer + sizeof query->buffer;

	bprintf(&start, end, "request %s %s\n", PACKAGE_NAME, VERSION);
	if (node)
		bprintf(&start, end, "node %s\n", node);
	if (service)
		bprintf(&start, end, "service %s\n", service);
	bprintf(&start, end, "\n");

	return query->buffer;
}

const char *
netresolve_get_path_string(netresolve_query_t query, int i)
{
	char *start = query->buffer;
	char *end = query->buffer + sizeof query->buffer;

	add_path(&start, end, query, i);

	return query->buffer;
}

const char *
netresolve_get_response_string(netresolve_query_t query)
{
	char *start = query->buffer;
	char *end = query->buffer + sizeof query->buffer;

	const char *nodename = netresolve_query_get_node_name(query);
	const char *servname = netresolve_query_get_service_name(query);
	size_t npaths = netresolve_query_get_count(query);
	size_t i;
	size_t length;
	const char *answer = netresolve_query_get_dns_answer(query, &length);

	bprintf(&start, end, "response %s %s\n", PACKAGE_NAME, VERSION);
	if (length) {
		bprintf(&start, end, "dns %d 0x", length);
		for (i = 0; i < length; i++)
			bprintf(&start, end, "%02hhx", answer[i]);
		bprintf(&start, end, "\n", length);
	}
	if (nodename)
		bprintf(&start, end, "name %s\n", nodename);
	if (servname)
		bprintf(&start, end, "service %s\n", servname);
	for (i = 0; i < npaths; i++) {
		add_path(&start, end, query, i);
		bprintf(&start, end, "\n");
	}
	bprintf(&start, end, "\n");

	return query->buffer;
}
