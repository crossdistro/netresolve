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
#include "common.h"

int
main(int argc, char **argv)
{
	netresolve_t context;
	netresolve_query_t query;
	const char *service = "80";
	int family = AF_UNSPEC;
	int socktype = 0;
	int protocol = IPPROTO_TCP;

	/* Create a context. */
	context = netresolve_context_new();
	if (!context) {
		perror("netresolve_context_new");
		abort();
	}

	/* Resolver configuration. */
	netresolve_context_set_options(context,
			NETRESOLVE_OPTION_FAMILY, family,
			NETRESOLVE_OPTION_SOCKTYPE, socktype,
			NETRESOLVE_OPTION_PROTOCOL, protocol,
			NULL);

	/* First query */
	query = netresolve_query_forward(context, "1:2:3:4:5:6:7:8%999999", service, NULL, NULL);
	check_address(query, AF_INET6, "1:2:3:4:5:6:7:8", 999999);
	netresolve_query_free(query);

	/* Second query */
	query = netresolve_query_forward(context, "1.2.3.4%999999", service, NULL, NULL);
	check_address(query, AF_INET, "1.2.3.4", 999999);
	netresolve_query_free(query);

	/* Check results */

	/* Clean up. */
	netresolve_context_free(context);

	exit(EXIT_SUCCESS);
}
