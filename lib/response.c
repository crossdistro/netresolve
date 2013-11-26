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
#include <string.h>
#include <assert.h>

#include "netresolve-private.h"

size_t
netresolve_get_path_count(netresolve_t resolver)
{
	return resolver->response.pathcount;
}

void
netresolve_get_path(netresolve_t resolver, size_t idx,
		int *family, const void **address, int *ifindex,
		int *socktype, int *protocol, int *port,
		int *priority, int *weight)
{
	assert (idx < resolver->response.pathcount);

	if (family)
		*family = resolver->response.paths[idx].node.family;
	if (address)
		*address = &resolver->response.paths[idx].node.address;
	if (ifindex)
		*ifindex = resolver->response.paths[idx].node.ifindex;
	if (socktype)
		*socktype = resolver->response.paths[idx].service.socktype;
	if (protocol)
		*protocol = resolver->response.paths[idx].service.protocol;
	if (port)
		*port = resolver->response.paths[idx].service.port;
	if (priority)
		*priority = resolver->response.paths[idx].priority;
	if (weight)
		*weight = resolver->response.paths[idx].weight;
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
	const void *address;

	netresolve_get_path(resolver, idx, &family, &address, &ifindex, socktype, protocol, &port, NULL, NULL);

	if (!address)
		return NULL;

	memset(&resolver->sa_buffer, 0, sizeof resolver->sa_buffer);

	switch (family) {
	case AF_INET:
		resolver->sa_buffer.sin.sin_family = family;
		resolver->sa_buffer.sin.sin_port = htons(port);
		resolver->sa_buffer.sin.sin_addr = *(struct in_addr *) address;
		if (salen)
			*salen = sizeof resolver->sa_buffer.sin;
		break;
	case AF_INET6:
		resolver->sa_buffer.sin6.sin6_family = family;
		resolver->sa_buffer.sin6.sin6_port = htons(port);
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

