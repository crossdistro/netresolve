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

/* FIXME: Would be nicer to only require `netresolve-compat.h`. */
#include <netresolve-private.h>

/* netresolve_query_getaddrinfo:
 *
 * Configures the channel and calls `netresolve_query()` according to the
 * input parameters which are the same as for POSIX `getaddrinfo()`. As this
 * function both depends on and changes the channel configuration, it's
 * recommended that a dedicated channel is created and used only for
 * `netresolve_query_getaddrinfo()` queries.
 *
 * When the query is finished, you can pick up the result using
 * `netresolve_query_getaddrinfo_done()` and free it using
 * `netresolve_query_getaddrinfo_free()`.
 */
netresolve_query_t
netresolve_query_getaddrinfo(netresolve_t channel, const char *node, const char *service, const struct addrinfo *hints)
{
	struct addrinfo default_hints = { 0 };

	if (!hints)
		hints = &default_hints;

	netresolve_set_default_loopback(channel, !(hints->ai_flags & AI_PASSIVE));
	netresolve_set_family(channel, hints->ai_family);
	netresolve_set_socktype(channel, hints->ai_socktype);
	netresolve_set_protocol(channel, hints->ai_protocol);

	return netresolve_query(channel, node, service);
}

int
netresolve_query_getaddrinfo_done(netresolve_query_t query, struct addrinfo **res, int32_t *ttlp)
{
	size_t npaths = netresolve_query_get_count(query);
	const char *canonname = netresolve_query_get_node_name(query);
	struct addrinfo head = {0};
	struct addrinfo *ai = &head;
	int i;

	if (ttlp)
		*ttlp = INT32_MAX;

	for (i = 0; i < npaths; i++) {
		int socktype, protocol;
		socklen_t salen;
		int32_t ttl;

		const struct sockaddr *sa = netresolve_query_get_sockaddr(query, i, &salen, &socktype, &protocol, &ttl);

		if (ttlp && ttl < *ttlp)
			*ttlp = ttl;

		ai = ai->ai_next = calloc(1, sizeof *ai + salen);
		if (!ai) {
			freeaddrinfo(head.ai_next);
			netresolve_query_done(query);
			return EAI_SYSTEM;
		}
		ai->ai_family = sa->sa_family;
		ai->ai_socktype = socktype;
		ai->ai_protocol = protocol;
		ai->ai_addrlen = salen;
		ai->ai_addr = (struct sockaddr *) (ai + 1);
		memcpy(ai->ai_addr, sa, salen);

		if (i == 0 && canonname)
			ai->ai_canonname = strdup(canonname);
	}

	netresolve_query_done(query);

	*res = head.ai_next;
	return *res ? 0 : EAI_NODATA;
}

netresolve_query_t
netresolve_query_getnameinfo(netresolve_t channel, const struct sockaddr *sa, socklen_t salen, int flags)
{
	struct sockaddr_in *sa4 = (void *) sa;
	struct sockaddr_in6 *sa6 = (void *) sa;

	switch (sa->sa_family) {
	case AF_INET:
		if (salen != sizeof *sa4)
			return NULL;
		return netresolve_query_reverse(channel, sa4->sin_family, &sa4->sin_addr, 0, ntohs(sa4->sin_port));
	case AF_INET6:
		if (salen != sizeof *sa6)
			return NULL;
		return netresolve_query_reverse(channel, sa6->sin6_family, &sa6->sin6_addr, sa6->sin6_scope_id, ntohs(sa6->sin6_port));
	default:
		return NULL;
	}
}

int
netresolve_query_getnameinfo_done(netresolve_query_t query, char **node, char **service, int32_t *ttl)
{
	const char *mynode = netresolve_query_get_node_name(query);
	const char *myservice = netresolve_query_get_service_name(query);

	if (node)
		*node = mynode ? strdup(mynode) : NULL;
	if (service)
		*service = myservice ? strdup(myservice) : NULL;

	netresolve_query_done(query);

	return 0;
}

/* netresolve_query_gethostbyname:
 *
 * Calls `netresolve_query()` to just resolve a node name. As its operation
 * depends on channel configuration, it's recommended that a dedicated channel
 * is created and used only for `netresolve_query_gethostbyname()` queries.
 *
 * When the query is finished, you can pick up the result using
 * `netresolve_query_gethostbyname_done()` and free using
 * `netresolve_query_gethostbyname_free()`.
 */
netresolve_query_t
netresolve_query_gethostbyname(netresolve_t channel, const char *name, int family)
{
	netresolve_set_family(channel, family);

	return netresolve_query(channel, name, NULL);
}

static struct hostent *
get_hostent(netresolve_query_t query, int *h_errnop, int32_t *ttlp)
{
	size_t npaths = netresolve_query_get_count(query);
	const char *canonname = netresolve_query_get_node_name(query);
	struct hostent *he = NULL;
	int idx;
	int n = 0;

	if (!(he = calloc(1, sizeof *he))) {
		*h_errnop = NO_RECOVERY;
		goto out;
	}

	if (ttlp)
		*ttlp = INT32_MAX;

	if (canonname)
		he->h_name = strdup(canonname);
	he->h_aliases = calloc(1, sizeof *he->h_aliases);
	he->h_addr_list = calloc(npaths + 1, sizeof *he->h_addr_list);

	for (idx = 0; idx < npaths; idx++) {
		int family, ifindex, socktype, protocol, port;
		int32_t ttl;
		const void *address;

		netresolve_query_get_node_info(query, idx, &family, &address, &ifindex);
		netresolve_query_get_service_info(query, idx, &socktype, &protocol, &port);
		netresolve_query_get_aux_info(query, idx, NULL, NULL, &ttl);

		if (ttlp && ttl < *ttlp)
			*ttlp = ttl;

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

out:
	netresolve_query_done(query);

	return he;
}

struct hostent *
netresolve_query_gethostbyname_done(netresolve_query_t query, int *h_errnop, int32_t *ttlp)
{
	size_t npaths = netresolve_query_get_count(query);

	if (!npaths) {
		*h_errnop = HOST_NOT_FOUND;
		return NULL;
	}

	return get_hostent(query, h_errnop, ttlp);
}

netresolve_query_t
netresolve_query_gethostbyaddr(netresolve_t channel, const void *address, int length, int family)
{
	switch (family) {
	case AF_INET:
		if (length != 4)
			return NULL;
		break;
	case AF_INET6:
		if (length != 16)
			return NULL;
	default:
		return NULL;
	}

	netresolve_query_t query = netresolve_query_reverse(channel, family, address, 0, 0);

	if (query)
		netresolve_backend_add_path(query, family, address, 0, 0, 0, 0, 0, 0, 0);

	return query;
}

struct hostent *
netresolve_query_gethostbyaddr_done(netresolve_query_t query, int *h_errnop, int32_t *ttlp)
{
	return get_hostent(query, h_errnop, ttlp);
}

void
netresolve_freeaddrinfo(struct addrinfo *ai)
{
	struct addrinfo *tmp;

	while (ai) {
		tmp = ai;
		ai = ai->ai_next;
		free(tmp->ai_canonname);
		free(tmp);
	}
}

void
netresolve_freehostent(struct hostent *he)
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
