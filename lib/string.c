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
	case SOCK_DGRAM:
		return "dgram";
	case SOCK_STREAM:
		return "stream";
	case SOCK_SEQPACKET:
		return "seqpacket";
	case SOCK_RAW:
		return "raw";
	default:
		return "0";
	}
}

static const char *
protocol_to_string(int proto)
{
	switch (proto) {
	case IPPROTO_UDP:
		return "udp";
	case IPPROTO_TCP:
		return "tcp";
	case IPPROTO_SCTP:
		return "sctp";
	default:
		return "0";
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
	int family, ifindex, socktype, protocol, port;
	const void *address = netresolve_get_path(resolver, i, &family, &ifindex, &socktype, &protocol, &port);
	char ifname[IF_NAMESIZE] = {0};
	char addrstr[1024] = {0};
	const char *socktypestr = socktype_to_string(socktype);
	const char *protocolstr = protocol_to_string(protocol);

	if (family == AF_INET || family == AF_INET6)
		inet_ntop(family, address, addrstr, sizeof addrstr);

	if (family == AF_UNIX)
		bprintf(start, end, "unix %s %s\n", address, socktypestr);
	else if (ifindex) {
		if (!if_indextoname(ifindex, ifname))
			snprintf(ifname, sizeof ifname, "%d", ifindex);
		if (socktype || protocol || port)
			bprintf(start, end, "path %s%%%s %s %s %d\n", addrstr, ifname, socktypestr, protocolstr, port);
		else
			bprintf(start, end, "address %s%%%s\n", addrstr, ifname);
	} else {
		if (socktype || protocol || port)
			bprintf(start, end, "path %s %s %s %d\n", addrstr, socktypestr, protocolstr, port);
		else
			bprintf(start, end, "address %s\n", addrstr);
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
