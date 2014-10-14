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
#include <netresolve-compat.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

/* getaddrinfo:
 *
 * Resolve `nodename` and `servname` using `hints` into a linked list of
 * address records including both L3 and L4 information. Canonical name,
 * if requested, is present in the first address record.
 *
 * The caller is responsible for freeing the data using `freeaddrinfo()`. The
 * function is reentrant, threat safe and doesn't leak memory.
 *
 * Defined in POSIX.1-2008.
 */
int
getaddrinfo(const char *nodename, const char *servname,
		const struct addrinfo *hints, struct addrinfo **result)
{
	netresolve_t channel;
	netresolve_query_t query;
	int status = EAI_SYSTEM;

	if (!(channel = netresolve_open()))
		return status;

	if ((query = netresolve_query_getaddrinfo(channel, nodename, servname, hints)))
		status = netresolve_query_getaddrinfo_done(query, result, NULL);

	netresolve_close(channel);
	return status;
}

/* freeaddrinfo:
 *
 * Free data returned by `getaddrinfo()`.
 *
 * Defined in POSIX.1-2008.
 */
void
freeaddrinfo(struct addrinfo *result)
{
	netresolve_query_getaddrinfo_free(result);
}

/* gethostbyname2:
 *
 * Extended version of `gethostbyname()` with the capability to select address
 * family.
 *
 * GNU extention to POSIX.1-2001 `gethostbyname()`
 */
struct hostent *
gethostbyname2(const char *node, int family)
{
	netresolve_t channel;
	netresolve_query_t query;
	static struct hostent *he = NULL;

	if (!(channel = netresolve_open()))
		return NULL;

	if ((query = netresolve_query_gethostbyname(channel, node, family))) {
		netresolve_query_gethostbyname_free(he);
		he = netresolve_query_gethostbyname_done(query, &errno, &h_errno, NULL);
	}

	netresolve_close(channel);
	return he;
}

/* gethostbyname:
 *
 * Caller doesn't free the result of this function. Instead, it is kept,
 * referenced by a static pointer, until the next call of `gethostbyname()`,
 * making `gethostbyname()` not reentrant by definition and the answer to the
 * last request is never freed.
 *
 * Defined in POSIX.1-2001, removed in POSIX.1-2008.
 */
struct hostent *
gethostbyname(const char *node)
{
	return gethostbyname2(node, AF_UNSPEC);
}

int
gethostbyname2_r(const char *name, int family,
		struct hostent *he, char *buffer, size_t buflen,
		struct hostent **result, int *h_errnop)
{
	netresolve_t channel;
	netresolve_query_t query;
	struct hostent *tmp;
	int lerrno;

	if (!(channel = netresolve_open())) {
		*result = NULL;
		return errno;
	}

	if ((query = netresolve_query_gethostbyname(channel, name, family)))
		tmp = netresolve_query_gethostbyname_done(query, &lerrno, h_errnop, NULL);

	size_t len_name = tmp->h_name ? strlen(tmp->h_name) + 1 : 0;
	size_t count = 0;
	for (char **alias = tmp->h_aliases; *alias; alias++)
		count++;
	size_t len_aliases = (count + 1) * sizeof *tmp->h_aliases;
	count = 0;
	for (char **addr = tmp->h_addr_list; *addr; addr++)
		count++;
	size_t len_addr_list = (count + 1) * sizeof *tmp->h_addr_list;
	size_t len_addr_data = count * tmp->h_length;
	size_t len_total = len_aliases + len_name + len_addr_list + len_addr_data;

	if (len_total > buflen) {
		*result = NULL;
		return ERANGE;
	}

	memcpy(he, tmp, sizeof *he);
	memset(buffer, 0, buflen);
	he->h_name = buffer;
	he->h_aliases = (void *) buffer + len_name;
	he->h_addr_list = (void *) buffer + len_name + len_aliases;
	memcpy(he->h_name, tmp->h_name, len_name);
	/* we don't allocate space for actual aliases */
	memset(he->h_aliases, 0, len_aliases);
	memcpy(he->h_addr_list, tmp->h_addr_list, len_addr_list);
	for (int i = 0; tmp->h_addr_list[i]; i++) {
		he->h_addr_list[i] = buffer + len_name + len_aliases + len_addr_list + i * tmp->h_length;
		memcpy(he->h_addr_list[i], tmp->h_addr_list[i], he->h_length);
	}

	netresolve_query_gethostbyname_free(tmp);
	netresolve_close(channel);

	*result = he;

	return 0;
}

int
gethostbyname_r(const char *name,
		struct hostent *he, char *buffer, size_t buflen,
		struct hostent **result, int *h_errnop)
{
	return gethostbyname2_r(name, AF_UNSPEC, he, buffer, buflen, result, h_errnop);
}
