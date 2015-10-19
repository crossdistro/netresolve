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
#include <asyncns.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	asyncns_t *asyncns;
	asyncns_query_t *query;
	struct addrinfo *result;
	struct pollfd pollfd = { .events = POLLIN };
	int status;

	asyncns = asyncns_new(10);
	assert(asyncns);
	assert(asyncns_getnqueries(asyncns) == 0);
	assert(asyncns_getnext(asyncns) == NULL);

	pollfd.fd = asyncns_fd(asyncns);
	assert(pollfd.fd > 2);

	query = asyncns_getaddrinfo(asyncns, "127.0.0.1", NULL, NULL);
	assert(query);
	assert(asyncns_getnqueries(asyncns) == 1);
	assert(asyncns_getnext(asyncns) == NULL);

	asyncns_cancel(asyncns, query);
	query = NULL;
	assert(asyncns_getnqueries(asyncns) == 0);
	assert(asyncns_getnext(asyncns) == NULL);

	query = asyncns_getaddrinfo(asyncns, "127.0.0.1", NULL, NULL);
	assert(query);
	assert(asyncns_getnqueries(asyncns) == 1);
	assert(asyncns_getnext(asyncns) == NULL);

	usleep(100000);
	status = poll(&pollfd, 1, 0);
	assert(status == 1);
	status = asyncns_wait(asyncns, 0);
	assert(status == 0);
	assert(asyncns_isdone(asyncns, query));
	assert(asyncns_getnqueries(asyncns) == 1);
	assert(asyncns_getnext(asyncns) == query);

	status = poll(&pollfd, 1, 100);
	assert(status == 0);

	status = asyncns_getaddrinfo_done(asyncns, query, &result);
	assert(asyncns_getnqueries(asyncns) == 0);

	/* Intuitively, this should not be needed but the docs state that
	 * a call to `asyncns_wait()` is necessary so that `asyncns_getnext()`
	 * provides meaningful results.
	 */
	status = asyncns_wait(asyncns, 0);
	assert(status == 0);

	/* There were two queries issued, one of which has been cancelled
	 * and the other has been freed afterwards. As none of them can be
	 * returned, the only meaningful result of `asyncns_getnext()` is
	 * NULL.
	 */
	assert(asyncns_getnext(asyncns) == NULL);

	asyncns_free(asyncns);
	asyncns_freeaddrinfo(result);

	return EXIT_SUCCESS;
}
