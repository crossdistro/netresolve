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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <netresolve.h>

static struct addrinfo *
generate_addrinfo_list(const netresolve_t resolver)
{
	size_t npaths = netresolve_get_path_count(resolver);
	const char *canonname = netresolve_get_canonical_name(resolver);
	struct addrinfo head = {0};
	struct addrinfo *ai = &head;
	int i;

	for (i = 0; i < npaths; i++) {
		int socktype, protocol;
		socklen_t salen;
		const struct sockaddr *sa = netresolve_get_path_sockaddr(resolver, i, &socktype, &protocol, &salen);

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
		const struct addrinfo *hints, struct addrinfo **res)
{
	netresolve_t resolver = netresolve_open();
	struct addrinfo default_hints = { 0 };
	int status;

	if (!hints)
		hints = &default_hints;

	netresolve_set_default_loopback(resolver, !(hints->ai_flags & AI_PASSIVE));

	status = netresolve_resolve(resolver, node, service, hints->ai_family, hints->ai_socktype, hints->ai_protocol);

	if (status)
		return EAI_SYSTEM;

	*res = generate_addrinfo_list(resolver);
	netresolve_close(resolver);

	return *res ? 0 : EAI_NODATA;
}

static struct hostent *
generate_hostent(netresolve_t resolver)
{
	size_t npaths = netresolve_get_path_count(resolver);
	const char *canonname = netresolve_get_canonical_name(resolver);
	struct hostent *he;
	int i;
	int n = 0;

	if (!npaths)
		return NULL;

	he = calloc(1, sizeof *he);
	if (canonname)
		he->h_name = strdup(canonname);
	he->h_aliases = calloc(1, sizeof *he->h_aliases);
	he->h_addr_list = calloc(npaths + 1, sizeof *he->h_addr_list);

	for (i = 0; i < npaths; i++) {
		int family, ifindex, socktype, protocol, port;
		const void *address;

		netresolve_get_path(resolver, i,
				&family, &address, &ifindex,
				&socktype, &protocol, &port,
				NULL, NULL);

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
	netresolve_t resolver = netresolve_open();
	int status;

	status = netresolve_resolve(resolver, node, NULL, AF_UNSPEC, 0, 0);

	freehostent(result);
	result = (status == 0) ? generate_hostent(resolver) : NULL;
	netresolve_close(resolver);

	return result;
}
