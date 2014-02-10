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
#include "common.h"

int
main(int argc, char **argv)
{
	netresolve_t channel;
	netresolve_query_t query;
	const char *service = "80";
	int family = AF_UNSPEC;
	int socktype = 0;
	int protocol = IPPROTO_TCP;

	/* Create a channel. */
	channel = netresolve_open();
	if (!channel) {
		perror("netresolve_open");
		abort();
	}

	/* Resolver configuration. */
	netresolve_set_family(channel, family);
	netresolve_set_socktype(channel, socktype);
	netresolve_set_protocol(channel, protocol);

	/* First query */
	query = netresolve_query(channel, "1:2:3:4:5:6:7:8%999999", service);
	check_address(query, AF_INET6, "1:2:3:4:5:6:7:8", 999999);
	netresolve_query_done(query);

	/* Second query */
	query = netresolve_query(channel, "1.2.3.4%999999", service);
	check_address(query, AF_INET, "1.2.3.4", 999999);
	netresolve_query_done(query);

	/* Check results */

	/* Clean up. */
	netresolve_close(channel);

	exit(EXIT_SUCCESS);
}
