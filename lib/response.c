#include <string.h>

#include "netresolve-private.h"

size_t
netresolve_get_path_count(netresolve_t resolver)
{
	return resolver->response.pathcount;
}

const void *
netresolve_get_path(netresolve_t resolver, size_t idx,
		int *family, int *ifindex, int *socktype, int *protocol, int *port)
{
	if (idx >= resolver->response.pathcount)
		return NULL;

	if (family)
		*family = resolver->response.paths[idx].node.family;
	if (ifindex)
		*ifindex = resolver->response.paths[idx].node.ifindex;
	if (socktype)
		*socktype = resolver->response.paths[idx].service.socktype;
	if (protocol)
		*protocol = resolver->response.paths[idx].service.protocol;
	if (port)
		*port = resolver->response.paths[idx].service.port;

	return &resolver->response.paths[idx].node.address;
}

const char *
netresolve_get_canonical_name(const netresolve_t resolver)
{
	return resolver->response.canonname;
}

const struct sockaddr *
netresolve_get_path_sockaddr(netresolve_t resolver, size_t idx,
		int *socktype, int *protocol, socklen_t *salen)
{
	int family, ifindex, port;
	const void *address = netresolve_get_path(resolver, idx, &family, &ifindex, socktype, protocol, &port);

	if (!address)
		return NULL;

	memset(&resolver->sa_buffer, 0, sizeof resolver->sa_buffer);

	switch (family) {
	case AF_INET:
		resolver->sa_buffer.sin.sin_family = family;
		resolver->sa_buffer.sin.sin_port = port;
		resolver->sa_buffer.sin.sin_addr = *(struct in_addr *) address;
		if (salen)
			*salen = sizeof resolver->sa_buffer.sin;
		break;
	case AF_INET6:
		resolver->sa_buffer.sin6.sin6_family = family;
		resolver->sa_buffer.sin6.sin6_port = port;
		resolver->sa_buffer.sin6.sin6_scope_id = ifindex;
		resolver->sa_buffer.sin6.sin6_addr = *(struct in6_addr *) address;
		if (salen)
			*salen = sizeof resolver->sa_buffer.sin6;
		break;
	default:
		return NULL;
	}

	return &resolver->sa_buffer.sa;
}

