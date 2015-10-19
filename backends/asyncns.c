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
#include <asyncns.h>

struct priv_asyncns {
	asyncns_t *asyncns;
};

static void
dispatch(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data)
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

static void
cleanup(void *data)
{
	struct priv_asyncns *priv = data;

	asyncns_free(priv->asyncns);
}

void
query_forward(netresolve_query_t query, char **settings)
{
	struct priv_asyncns *priv = netresolve_backend_new_priv(query, sizeof *priv, cleanup);
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

	netresolve_watch_add(query, asyncns_fd(priv->asyncns), POLLIN, dispatch, NULL);

	return;
}
