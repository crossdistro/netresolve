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
#include <netresolve-backend.h>
#include <stdlib.h>
#include <arpa/inet.h>

static bool
is_any(int family, const void *address)
{
	switch (family) {
	case AF_INET:
		return !memcmp(address, &inaddr_any, sizeof inaddr_any);
	case AF_INET6:
		return !memcmp(address, &in6addr_any, sizeof in6addr_any);
	default:
		return false;
	}
}

void
query_forward(netresolve_query_t query, char **settings)
{
	const char *node = netresolve_backend_get_nodename(query);

	/* skip if requested */
	if (netresolve_backend_get_default_loopback(query)) {
		netresolve_backend_failed(query);
		return;
	}

	if (node && *node) {
		netresolve_backend_failed(query);
		return;
	}

	netresolve_backend_add_path(query, AF_INET, &inaddr_any, 0, 0, 0, 0, 0, 0, 0);
	netresolve_backend_add_path(query, AF_INET6, &in6addr_any, 0, 0, 0, 0, 0, 0, 0);
	netresolve_backend_set_secure(query);
	netresolve_backend_finished(query);
}

void
query_reverse(netresolve_query_t query, char **settings)
{
	int family = netresolve_backend_get_family(query);
	const void *address = netresolve_backend_get_address(query);

	/* skip if requested */
	if (netresolve_backend_get_default_loopback(query)) {
		netresolve_backend_failed(query);
		return;
	}

	if (!is_any(family, address)) {
		netresolve_backend_failed(query);
		return;
	}

	netresolve_backend_add_name_info(query, "", NULL);
	netresolve_backend_set_secure(query);
	netresolve_backend_finished(query);
}
