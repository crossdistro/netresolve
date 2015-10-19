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
#include <sys/utsname.h>
#include <ifaddrs.h>

static bool
add_addresses(netresolve_query_t query, struct ifaddrs *list, bool filter)
{
	bool result = false;

	for (struct ifaddrs *item = list; item; item = item->ifa_next) {
		struct sockaddr *sa = item->ifa_addr;
		int family = sa->sa_family;
		void *address = NULL;
		int ifindex = 0;

		debug("found address: dev %s family %d", item->ifa_name, family);

		switch (family) {
		case AF_INET:
			address = &((struct sockaddr_in *) sa)->sin_addr;
			if (filter && !memcmp(address, &inaddr_loopback, sizeof inaddr_loopback))
				continue;
			break;
		case AF_INET6:
			address = &((struct sockaddr_in6 *) sa)->sin6_addr;
			ifindex = ((struct sockaddr_in6 *) sa)->sin6_scope_id;
			if (filter) {
				if (!memcmp(address, &in6addr_loopback, sizeof inaddr_loopback))
					continue;
				if (ifindex)
					continue;
			}
			break;
		default:
			continue;
		}

		netresolve_backend_add_path(query, family, address, ifindex, 0, 0, 0, 0, 0, 0);
		result = true;
	}

	return result;
}

void
query_forward(netresolve_query_t query, char **settings)
{
	const char *nodename = netresolve_backend_get_nodename(query);
	struct ifaddrs *list;
	struct utsname name;

	uname(&name);
	if (!nodename || strcmp(nodename, name.nodename)) {
		netresolve_backend_failed(query);
		return;
	}

	if (getifaddrs(&list)) {
		netresolve_backend_failed(query);
		return;
	}

	if (!add_addresses(query, list, true))
		add_addresses(query, list, false);

	netresolve_backend_set_canonical_name(query, nodename);
	netresolve_backend_finished(query);
}
