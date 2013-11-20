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
netresolve_query_get_count(netresolve_query_t query)
{
	return query->response.pathcount;
}

void
netresolve_query_get_address_info(netresolve_query_t query, size_t idx,
		int *family, const void **address, int *ifindex)
{
	assert (idx < query->response.pathcount);

	if (family)
		*family = query->response.paths[idx].node.family;
	if (address)
		*address = &query->response.paths[idx].node.address;
	if (ifindex)
		*ifindex = query->response.paths[idx].node.ifindex;
}

void
netresolve_query_get_port_info(netresolve_query_t query, size_t idx,
		int *socktype, int *protocol, int *port)
{
	assert (idx < query->response.pathcount);

	if (socktype)
		*socktype = query->response.paths[idx].service.socktype;
	if (protocol)
		*protocol = query->response.paths[idx].service.protocol;
	if (port)
		*port = query->response.paths[idx].service.port;
}

void
netresolve_query_get_aux_info(netresolve_query_t query, size_t idx,
		int *priority, int *weight, int *ttl)
{
	assert (idx < query->response.pathcount);

	if (priority)
		*priority = query->response.paths[idx].priority;
	if (weight)
		*weight = query->response.paths[idx].weight;
	if (ttl)
		*ttl = query->response.paths[idx].ttl;
}

const char *
netresolve_query_get_canonical_name(const netresolve_query_t query)
{
	return query->response.canonname;
}

const struct sockaddr *
netresolve_query_get_path_sockaddr(netresolve_query_t query, size_t idx,
		socklen_t *salen, int *socktype, int *protocol)
{
	int family, ifindex, port;
	const void *address;

	netresolve_query_get_address_info(query, idx, &family, &address, &ifindex);
	netresolve_query_get_port_info(query, idx, socktype, protocol, &port);

	if (!address)
		return NULL;

	memset(&query->sa_buffer, 0, sizeof query->sa_buffer);

	switch (family) {
	case AF_INET:
		query->sa_buffer.sin.sin_family = family;
		query->sa_buffer.sin.sin_port = htons(port);
		query->sa_buffer.sin.sin_addr = *(struct in_addr *) address;
		if (salen)
			*salen = sizeof query->sa_buffer.sin;
		break;
	case AF_INET6:
		query->sa_buffer.sin6.sin6_family = family;
		query->sa_buffer.sin6.sin6_port = htons(port);
		query->sa_buffer.sin6.sin6_scope_id = ifindex;
		query->sa_buffer.sin6.sin6_addr = *(struct in6_addr *) address;
		if (salen)
			*salen = sizeof query->sa_buffer.sin6;
		break;
	default:
		return NULL;
	}

	return &query->sa_buffer.sa;
}

