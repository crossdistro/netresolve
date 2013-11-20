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
#include <assert.h>
#include <asyncns.h>
#include <poll.h>

int
main(int argc, char **argv)
{
	const char *node = "1:2:3:4:5:6:7:8%999999";
	const char *service = "80";
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = 0,
		.ai_protocol = IPPROTO_TCP,
		.ai_flags = AI_CANONNAME,
	};
	struct addrinfo *result = NULL;
	int status;
	asyncns_t *asyncns;
	asyncns_query_t *q;
	int fd;

	asyncns = asyncns_new(10);
	assert(asyncns);
	fd = asyncns_fd(asyncns);
	assert(fd > 2);
	assert(asyncns_getnqueries(asyncns) == 0);
	assert(asyncns_getnext(asyncns) == NULL);

	q = asyncns_getaddrinfo(asyncns, node, service, &hints);
	assert(asyncns_getnqueries(asyncns) == 1);
	assert(asyncns_getnext(asyncns) == NULL);
	assert(!asyncns_isdone(asyncns, q));

	asyncns_cancel(asyncns, q);
	assert(asyncns_getnqueries(asyncns) == 0);
	assert(asyncns_getnext(asyncns) == NULL);
	assert(!asyncns_isdone(asyncns, q));

	q = asyncns_getaddrinfo(asyncns, node, service, &hints);
	assert(asyncns_getnqueries(asyncns) == 1);
	assert(asyncns_getnext(asyncns) == NULL);
	assert(!asyncns_isdone(asyncns, q));

	struct pollfd pollfd = { .fd = fd, .events = POLLIN };
	status = poll(&pollfd, 1, 1);
	assert(status == 1);
	status = asyncns_wait(asyncns, 0);
	assert(status == 0);
	assert(asyncns_getnqueries(asyncns) == 1);
	assert(asyncns_getnext(asyncns) == q);
	assert(asyncns_isdone(asyncns, q));

	status = asyncns_getaddrinfo_done(asyncns, q, &result);
	assert(asyncns_getnqueries(asyncns) == 0);
	/* Looks like there's a bug in the upstram implementation.
	 *
	 * assert(asyncns_getnext(asyncns) == NULL);
	 */

	asyncns_free(asyncns);

	assert(status == 0);
	assert(result);
	struct { struct addrinfo ai; struct sockaddr_in6 sa; } expected = {
		.ai = {
			.ai_family = AF_INET6,
			.ai_socktype = SOCK_STREAM,
			.ai_protocol = IPPROTO_TCP,
			.ai_addr = result->ai_addr,
			.ai_addrlen = sizeof expected.sa,
			.ai_canonname = result->ai_canonname
		},
		.sa = {
			.sin6_family = AF_INET6,
			.sin6_port = htons(80),
			.sin6_addr = { .s6_addr = { 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8 } },
			.sin6_scope_id = 999999
		}
	};
	/* Another bugs in upstream asyncns */
	if (result->ai_flags == 2)
		result->ai_flags = 0;

	assert(!memcmp(result, &expected.ai, sizeof expected.ai));
	assert(result->ai_canonname && !strcmp(result->ai_canonname, node));
	assert(!memcmp(result->ai_addr, &expected.sa, sizeof expected.sa));

	asyncns_freeaddrinfo(result);

	return EXIT_SUCCESS;
}
