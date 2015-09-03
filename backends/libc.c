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
#include <resolv.h>

#define SIZE 16 * 1024

void
query_forward(netresolve_query_t query, char **settings)
{
	const char *node = netresolve_backend_get_nodename(query);
	const char *service = netresolve_backend_get_servname(query);
	struct addrinfo hints = netresolve_backend_get_addrinfo_hints(query);
	int status;
	struct addrinfo *result;

	if ((status = getaddrinfo(node, service, &hints, &result))) {
		netresolve_backend_failed(query);
		return;
	}

	netresolve_backend_apply_addrinfo(query, status, result, 0);
}

void
query_reverse(netresolve_query_t query, char **settings)
{
	int flags = 0;
	union {
		struct sockaddr sa;
		struct sockaddr_in sa4;
		struct sockaddr_in6 sa6;
	} sa = { { 0 } };
	int status;
	char nodename[SIZE], servname[SIZE];

	sa.sa.sa_family = netresolve_backend_get_family(query);

	switch (sa.sa.sa_family) {
	case AF_INET:
		memcpy(&sa.sa4.sin_addr, netresolve_backend_get_address(query), sizeof sa.sa4.sin_addr);
		sa.sa4.sin_port = netresolve_backend_get_port(query);
		status = getnameinfo(&sa.sa, sizeof sa.sa4, nodename, sizeof nodename, servname, sizeof servname, flags);
		break;
	case AF_INET6:
		memcpy(&sa.sa6.sin6_addr, netresolve_backend_get_address(query), sizeof sa.sa6.sin6_addr);
		sa.sa6.sin6_port = netresolve_backend_get_port(query);
		status = getnameinfo(&sa.sa, sizeof sa.sa4, nodename, sizeof nodename, servname, sizeof servname, flags);
		break;
	default:
		status = EAI_FAMILY;
		break;
	}

	if (status) {
		netresolve_backend_failed(query);
		return;
	}

	netresolve_backend_add_name_info(query, nodename, servname);
	netresolve_backend_finished(query);
}

void
query_dns(netresolve_query_t query, char **settings)
{
	const char *dname = netresolve_backend_get_nodename(query);
	bool search = netresolve_backend_get_dns_search(query);
	int cls;
	int type;
	uint8_t answer[SIZE];
	size_t length;

	netresolve_backend_get_dns_query(query, &cls, &type);

	length = (search ? res_search : res_query)(dname, cls, type, answer, sizeof answer);

	if (length == -1) {
		netresolve_backend_failed(query);
		return;
	}

	netresolve_backend_set_dns_answer(query, answer, length);
	netresolve_backend_finished(query);
}
