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
#include <netresolve-backend.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

static void
add_addresses(netresolve_query_t query, const struct addrinfo *first)
{
	const struct addrinfo *ai;

	for (ai = first; ai; ai = ai->ai_next) {
		union {
			struct in_addr address4;
			struct in6_addr address6;
		} address = {{0}};
		int ifindex = 0;
		int port = 0;

		if (!ai->ai_family)
			continue;

		switch (ai->ai_family) {
		case AF_INET:
			memcpy(&address.address4, &((struct sockaddr_in *) ai->ai_addr)->sin_addr, sizeof address.address4);
			port = ntohs(((struct sockaddr_in *) ai->ai_addr)->sin_port);
			break;
		case AF_INET6:
			memcpy(&address.address6, &((struct sockaddr_in6 *) ai->ai_addr)->sin6_addr, sizeof address.address6);
			if (IN6_IS_ADDR_LINKLOCAL(&address.address6))
				ifindex = ((struct sockaddr_in6 *) ai->ai_addr)->sin6_scope_id;
			port = ntohs(((struct sockaddr_in6 *) ai->ai_addr)->sin6_port);
			break;
		}

		netresolve_backend_add_path(query,
				ai->ai_family, &address, ifindex,
				ai->ai_socktype, ai->ai_protocol, port,
				0, 0, 0);
	}

}

void
start(netresolve_query_t query, char **settings)
{
	const char *node = netresolve_backend_get_nodename(query);
	const char *service = netresolve_backend_get_servname(query);
	struct addrinfo hints = {
		.ai_family = netresolve_backend_get_family(query),
		.ai_socktype = netresolve_backend_get_socktype(query),
		.ai_protocol = netresolve_backend_get_protocol(query),
		.ai_flags = 0,
	};
	struct addrinfo *result;
	int status;

	if (!netresolve_backend_get_default_loopback(query))
		hints.ai_flags |= AI_PASSIVE;

	status = getaddrinfo(node, service, &hints, &result);

	switch (status) {
	case 0:
		add_addresses(query, result);
		freeaddrinfo(result);
		/* fall through */
	case EAI_NONAME:
	//case EAI_NODATA:
		netresolve_backend_finished(query);
		break;
	default:
		netresolve_backend_failed(query);
	}
}
