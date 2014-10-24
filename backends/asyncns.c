/* This file is part of the `netresolve` library.
 * Copyright (C) Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <netresolve-backend.h>
#include <asyncns.h>

struct priv_asyncns {
	asyncns_t *asyncns;
};

void
setup_forward(netresolve_query_t query, char **settings)
{
	struct priv_asyncns *priv = netresolve_backend_new_priv(query, sizeof *priv);
	const char *nodename = netresolve_backend_get_nodename(query);
	const char *servname = netresolve_backend_get_servname(query);
	struct addrinfo hints = netresolve_backend_get_addrinfo_hints(query);

	if (!priv || !(priv->asyncns = asyncns_new(2))) {
		netresolve_backend_failed(query);
		return;
	}

	if (!asyncns_getaddrinfo(priv->asyncns, nodename, servname, &hints)) {
		netresolve_backend_failed(query);
		return;
	}

	netresolve_backend_watch_fd(query, asyncns_fd(priv->asyncns), POLLIN);

	return;
}

void
dispatch(netresolve_query_t query, int fd, int events)
{
	struct priv_asyncns *priv = netresolve_backend_get_priv(query);
	asyncns_query_t *q;
	struct addrinfo *result;
	int status;

	asyncns_wait(priv->asyncns, 0);
	if (!(q = asyncns_getnext(priv->asyncns)))
		return;

	status = asyncns_getaddrinfo_done(priv->asyncns, q, &result);

	netresolve_backend_apply_addrinfo(query, status, result, 0);
	asyncns_freeaddrinfo(result);
}

void
cleanup(netresolve_query_t query)
{
	struct priv_asyncns *priv = netresolve_backend_get_priv(query);

	asyncns_free(priv->asyncns);
}
