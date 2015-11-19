/* Copyright (c) 2013+ Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
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
#include <string.h>
#include <netdb.h>
#include <nss.h>
#include <errno.h>
#include <arpa/inet.h>

int
_nss_netresolve_getaddrinfo(const char *nodename, const char *servname,
		const struct addrinfo *hints,
		struct addrinfo **result, int32_t *ttl)
{
	netresolve_t context;
	netresolve_query_t query;
	int status = EAI_SYSTEM;

	if (!(context = netresolve_context_new()))
		return status;

	if ((query = netresolve_query_getaddrinfo(context, nodename, servname, hints, NULL, NULL)))
		status = netresolve_query_getaddrinfo_done(query, result, ttl);

	netresolve_context_free(context);
	return status;
}

struct buffer_t {
	char *data;
	size_t length;
	int *errnop;
};

static void *
buffer_append(const void *data, size_t size, struct buffer_t *buffer)
{
	void *position = buffer->data;

	if (data && !size)
		size = strlen(data) + 1;

	if (buffer->length < size) {
		*buffer->errnop = ERANGE;
		return NULL;
	}

	buffer->data += size;
	buffer->length -= size;

	if (data)
		memcpy(position, data, size);
	else
		memset(position, 0, size);

	return position;
}

static void
extract_address(struct addrinfo *item, void *address)
{
	if (address) {
		switch (item->ai_family) {
		case AF_INET:
			memcpy(address, &((struct sockaddr_in *) (item->ai_addr))->sin_addr, sizeof (struct in_addr));
			return;
		case AF_INET6:
			memcpy(address, &((struct sockaddr_in6 *) (item->ai_addr))->sin6_addr, sizeof (struct in6_addr));
			return;
		}
	}
}

enum nss_status
_nss_netresolve_gethostbyname4_r(const char *nodename,
	struct gaih_addrtuple **result,
	char *bufptr, size_t buflen, int *errnop,
	int *h_errnop, int32_t *ttlp)
{
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_RAW,
		.ai_flags = AI_CANONNAME
	};
	struct buffer_t buffer = { .data = bufptr, .length = buflen, .errnop = errnop };
	int status;
	struct addrinfo *list;

	status = _nss_netresolve_getaddrinfo(nodename, NULL, &hints, &list, ttlp);

	if (status)
		return NSS_STATUS_UNAVAIL;

	/* addresses */
	for (struct addrinfo *item = list; item; item = item->ai_next) {
		if (!(*result = buffer_append(NULL, sizeof **result, &buffer)))
			return NSS_STATUS_TRYAGAIN;

		(*result)->family = item->ai_family;
		extract_address(item, &(*result)->addr);

		/* canonical name piggybacking on the first address */
		if (item == list && list->ai_canonname)
			if (!((*result)->name = buffer_append(list->ai_canonname, 0, &buffer)))
				return NSS_STATUS_TRYAGAIN;

		result = &(*result)->next;
	}

	return NSS_STATUS_SUCCESS;
}

enum nss_status
_nss_netresolve_gethostbyname3_r(const char *nodename, int family,
		struct hostent *he,
		char *bufptr, size_t buflen, int *errnop,
		int *h_errnop, int32_t *ttlp, char **canonp)
{
	struct addrinfo hints = {
		.ai_family = family,
		.ai_socktype = SOCK_RAW,
		.ai_flags = AI_CANONNAME
	};
	struct buffer_t buffer = { .data = bufptr, .length = buflen, .errnop = errnop };
	int status;
	struct addrinfo *list;
	int count;

	status = _nss_netresolve_getaddrinfo(nodename, NULL, &hints, &list, ttlp);

	if (status)
		return NSS_STATUS_UNAVAIL;

	/* address family */
	he->h_addrtype = family;
	he->h_length = (family == AF_INET) ? 4 : 16;

	/* pointers to addresses */
	count = 0;
	for (struct addrinfo *item = list; item; item = item->ai_next)
		count++;
	if (!(he->h_addr_list = buffer_append(NULL, (count + 1) * sizeof *he->h_addr_list, &buffer)))
		return NSS_STATUS_TRYAGAIN;

	/* addresses */
	count = 0;
	for (struct addrinfo *item = list; item; item = item->ai_next) {
		if (item->ai_family != family)
			continue;

		if (!(he->h_addr_list[count] = buffer_append(NULL, he->h_length, &buffer)))
			return NSS_STATUS_TRYAGAIN;

		extract_address(item, he->h_addr_list[count]);

		count++;
	}

	/* canonical name */
	if (list->ai_canonname)
		if (!(he->h_name = buffer_append(list->ai_canonname, 0, &buffer)))
			return NSS_STATUS_TRYAGAIN;

	return NSS_STATUS_SUCCESS;
}

enum nss_status
_nss_netresolve_gethostbyname2_r(const char *nodename, int family,
		struct hostent *he,
		char *buffer, size_t buflen, int *errnop, int *h_errnop)
{
	return _nss_netresolve_gethostbyname3_r(nodename, family, he, buffer, buflen, errnop, h_errnop, NULL, NULL);
}

enum nss_status
_nss_netresolve_gethostbyname_r(const char *nodename,
		struct hostent *he,
		char *buffer, size_t buflen, int *errnop, int *h_errnop)
{
	return _nss_netresolve_gethostbyname2_r(nodename, AF_INET, he, buffer, buflen, errnop, h_errnop);
}
