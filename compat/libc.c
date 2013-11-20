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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <netresolve.h>

static struct addrinfo *
generate_addrinfo_list(const netresolve_query_t query)
{
	size_t npaths = netresolve_query_get_count(query);
	const char *canonname = netresolve_query_get_canonical_name(query);
	struct addrinfo head = {0};
	struct addrinfo *ai = &head;
	int i;

	for (i = 0; i < npaths; i++) {
		int socktype, protocol;
		socklen_t salen;
		const struct sockaddr *sa = netresolve_query_get_path_sockaddr(query, i, &salen, &socktype, &protocol);

		ai = ai->ai_next = calloc(1, sizeof *ai + salen);
		if (!ai)
			goto fail;
		ai->ai_family = sa->sa_family;
		ai->ai_socktype = socktype;
		ai->ai_protocol = protocol;
		ai->ai_addrlen = salen;
		ai->ai_addr = (struct sockaddr *) (ai + 1);
		memcpy(ai->ai_addr, sa, salen);

		if (i == 0 && canonname)
			ai->ai_canonname = strdup(canonname);
	}

	return head.ai_next;
fail:
	freeaddrinfo(head.ai_next);
	return NULL;
}

int
getaddrinfo(const char *node, const char *service,
		const struct addrinfo *hints, struct addrinfo **result)
{
	netresolve_t channel = netresolve_open();
	struct addrinfo default_hints = { 0 };
	netresolve_query_t query;

	if (!hints)
		hints = &default_hints;

	netresolve_set_default_loopback(channel, !(hints->ai_flags & AI_PASSIVE));
	netresolve_set_family(channel, hints->ai_family);
	netresolve_set_socktype(channel, hints->ai_socktype);
	netresolve_set_protocol(channel, hints->ai_protocol);

	if (!(query = netresolve_query(channel, node, service))) {
		netresolve_close(channel);
		return EAI_SYSTEM;
	}

	*result = generate_addrinfo_list(query);
	netresolve_close(channel);

	return *result ? 0 : EAI_NODATA;
}

static struct hostent *
generate_hostent(netresolve_query_t query)
{
	size_t npaths = netresolve_query_get_count(query);
	const char *canonname = netresolve_query_get_canonical_name(query);
	struct hostent *he;
	int idx;
	int n = 0;

	if (!npaths)
		return NULL;

	he = calloc(1, sizeof *he);
	if (canonname)
		he->h_name = strdup(canonname);
	he->h_aliases = calloc(1, sizeof *he->h_aliases);
	he->h_addr_list = calloc(npaths + 1, sizeof *he->h_addr_list);

	for (idx = 0; idx < npaths; idx++) {
		int family, ifindex, socktype, protocol, port;
		const void *address;

		netresolve_query_get_address_info(query, idx, &family, &address, &ifindex);
		netresolve_query_get_port_info(query, idx, &socktype, &protocol, &port);

		if (family != AF_INET && family != AF_INET6)
			continue;

		if (he->h_addrtype) {
			if (he->h_addrtype != family)
				continue;
		} else {
			he->h_addrtype = family;
			he->h_length = family == AF_INET ? 4 : 16;
		}

		he->h_addr_list[n] = calloc(1, he->h_length);
		if (!he->h_addr_list[n])
			break;
		memcpy(he->h_addr_list[n], address, he->h_length);
		n++;
	}

	return he;
}

static void
freehostent(struct hostent *he)
{
	char **p;

	if (!he)
		return;

	free(he->h_name);
	for (p = he->h_aliases; *p; p++)
		free(*p);
	free(he->h_aliases);
	for (p = he->h_addr_list; *p; p++)
		free(*p);
	free(he->h_addr_list);
}

struct hostent *
gethostbyname(const char *node)
{
	static struct hostent *result = NULL;
	netresolve_t channel;
	netresolve_query_t query;
   
	if (!(channel = netresolve_open()))
		return NULL;

	query = netresolve_query(channel, node, NULL);

	freehostent(result);
	result = query ? generate_hostent(query) : NULL;

	netresolve_close(channel);

	return result;
}
