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
#include <asyncns.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

static void
check(int status, struct addrinfo *result, int family, const char *nodename)
{
	assert(status == 0);
	assert(result);
	struct { struct addrinfo ai; struct sockaddr_in sa4; struct sockaddr_in6 sa6; } expected = {
		.ai = {
			.ai_family = family,
			.ai_socktype = SOCK_STREAM,
			.ai_protocol = IPPROTO_TCP,
			.ai_addr = result->ai_addr,
			.ai_addrlen = family == AF_INET6 ? sizeof expected.sa6 : sizeof expected.sa4,
			.ai_canonname = result->ai_canonname
		},
		.sa4 = {
			.sin_family = AF_INET,
			.sin_port = htons(80),
			.sin_addr = { htonl(0x01020304) }
		},
		.sa6 = {
			.sin6_family = AF_INET6,
			.sin6_port = htons(80),
			.sin6_addr = { .s6_addr = { 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8 } },
			.sin6_scope_id = 999999
		}
	};
	/* getaddrinfo returns original ai_flags */
	if (result->ai_flags == 2)
		result->ai_flags = 0;

	assert(!memcmp(result, &expected.ai, sizeof expected.ai));
	assert(result->ai_canonname && !strcmp(result->ai_canonname, nodename));
	assert(!memcmp(result->ai_addr, family == AF_INET6 ? (void *) &expected.sa6 : (void *) &expected.sa4, expected.ai.ai_addrlen));
}

int
main(int argc, char **argv)
{
	const char *nodename1 = "1:2:3:4:5:6:7:8%999999";
	const char *nodename2 = "1.2.3.4";
	const char *servname = "80";
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = 0,
		.ai_protocol = IPPROTO_TCP,
		.ai_flags = AI_CANONNAME,
	};
	struct addrinfo *result1 = NULL, *result2 = NULL;
	int status, status1, status2;
	asyncns_t *asyncns;
	asyncns_query_t *q0, *q1, *q2;
	int fd;

	/* Create the thread pool */
	asyncns = asyncns_new(10);
	assert(asyncns);
	assert(asyncns_getnqueries(asyncns) == 0);
	assert(asyncns_getnext(asyncns) == NULL);

	/* Acquire the file descriptor */
	fd = asyncns_fd(asyncns);
	assert(fd > 2);

	/* Start a query and cancel it */
	if (!argv[1] || strcmp(argv[1], "--skip-cancel")) {
		/* Start a query and cancel it */
		q0 = asyncns_getaddrinfo(asyncns, nodename1, servname, &hints);
		assert(asyncns_getnqueries(asyncns) == 1);
		assert(asyncns_getnext(asyncns) == NULL);
		assert(!asyncns_isdone(asyncns, q0));

		asyncns_cancel(asyncns, q0);
		assert(asyncns_getnqueries(asyncns) == 0);
		assert(asyncns_getnext(asyncns) == NULL);
	}

	/* Start first real query */
	q1 = asyncns_getaddrinfo(asyncns, nodename1, servname, &hints);
	assert(asyncns_getnqueries(asyncns) == 1);
	assert(asyncns_getnext(asyncns) == NULL);
	assert(!asyncns_isdone(asyncns, q1));

	/* Start second real query */
	q2 = asyncns_getaddrinfo(asyncns, nodename2, servname, &hints);
	assert(q2 != q1);
	assert(asyncns_getnqueries(asyncns) == 2);
	assert(asyncns_getnext(asyncns) == NULL);
	assert(!asyncns_isdone(asyncns, q2));

	/* Poll for the results */
	while (!asyncns_isdone(asyncns, q1) || !asyncns_isdone(asyncns, q2)) {
		struct pollfd pollfd = { .fd = fd, .events = POLLIN };

		status = poll(&pollfd, 1, -1);
		assert(status == 1);

		status = asyncns_wait(asyncns, 0);
		assert(status == 0);
	}

	/* Both queries are finished and one is available via `asyncns_getnext()` */
	assert(asyncns_getnqueries(asyncns) == 2);
	assert(asyncns_isdone(asyncns, q1) && asyncns_isdone(asyncns, q2));
	assert(asyncns_getnext(asyncns) == q1 || asyncns_getnext(asyncns) == q2);

	/* Destroy the first query */
	status1 = asyncns_getaddrinfo_done(asyncns, q1, &result1);
	assert(asyncns_getnqueries(asyncns) == 1);

	/* The docs seem to require `asyncns_wait()` before `asyncns_getnext()`
	 * so lets' call it just in case.
	 */
	status = asyncns_wait(asyncns, 0);
	assert(status == 0);

	/* Check that the first query is no longer there */
	assert(asyncns_getnext(asyncns) != q1);

	/* Now second query should be available via `asycns_getnext()` */
	assert(asyncns_getnext(asyncns) == q2);

	/* Destry the second query */
	status2 = asyncns_getaddrinfo_done(asyncns, q2, &result2);
	assert(asyncns_getnqueries(asyncns) == 0);

	/* The docs seem to require `asyncns_wait()` before `asyncns_getnext()`
	 * so lets' call it just in case.
	 */
	status = asyncns_wait(asyncns, 0);
	assert(status == 0);

	/* Check that the old queries are no longer there */
	assert(asyncns_getnext(asyncns) != q1);
	assert(asyncns_getnext(asyncns) != q2);

	/* Now no more queries should be available via `asycns_getnext()` */
	assert(asyncns_getnext(asyncns) == NULL);

	/* Destroy the pool */
	asyncns_free(asyncns);

	/* Check result data and destroy it */
	check(status1, result1, AF_INET6, nodename1);
	check(status2, result2, AF_INET, nodename2);
	asyncns_freeaddrinfo(result1);
	asyncns_freeaddrinfo(result2);

	return EXIT_SUCCESS;
}
