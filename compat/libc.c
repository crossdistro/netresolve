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
		const struct addrinfo *hints, struct addrinfo **res)
{
	netresolve_t channel;
	netresolve_query_t query;
	int status = EAI_SYSTEM;

	if (!(channel = netresolve_open()))
		return status;

	if ((query = netresolve_query_getaddrinfo(channel, nodename, servname, hints)))
		status = netresolve_query_getaddrinfo_done(query, res, NULL);

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
freeaddrinfo(struct addrinfo *ai)
{
	netresolve_query_getaddrinfo_free(ai);
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
	netresolve_t channel;
	netresolve_query_t query;
	static struct hostent *he = NULL;

	if (!(channel = netresolve_open()))
		return he;

	if ((query = netresolve_query_gethostbyname(channel, node))) {
		netresolve_query_gethostbyname_free(he);
		he = netresolve_query_gethostbyname_done(query, &h_errno);
	}

	netresolve_close(channel);
	return he;
}
