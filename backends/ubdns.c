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
#include <netresolve-backend.h>
#include <unbound.h>
#include <arpa/nameser.h>
#include <stdlib.h>
#include <stdio.h>

struct priv_ubdns {
	struct ub_ctx* ctx;
	bool finished4;
	bool finished6;
};

struct priv_address_lookup {
	netresolve_query_t query;
};

static void
host_callback(void *arg, int status, struct ub_result* result)
{
	struct priv_address_lookup *lookup_data = arg;
	netresolve_query_t query = lookup_data->query;
	struct priv_ubdns *priv = netresolve_backend_get_priv(query);
	int family = result->qtype == ns_t_a ? AF_INET : AF_INET6;

	switch (status) {
	case 0:
		if (result->havedata) {
			char **data;

			for (data = result->data; *data; data++)
				netresolve_backend_add_path(query, family, *data, 0, 0, 0, 0, 0, 0, result->ttl);
		}
		ub_resolve_free(result);
		break;
	default:
		error("libunbound: %s\n", ub_strerror(status));
	}

	if (family == AF_INET)
		priv->finished4 = true;
	else
		priv->finished6 = true;
}

static void
start_address_lookup(netresolve_query_t query)
{
	struct priv_ubdns *priv = netresolve_backend_get_priv(query);
	const char *node = netresolve_backend_get_nodename(query);
	struct priv_address_lookup *lookup_data = calloc(1, sizeof *lookup_data);

	if (!lookup_data) {
		netresolve_backend_failed(query);
		return;
	}

	lookup_data->query = query;

	debug("Looking up %d %d %s\n", ns_t_a, ns_c_in, node);
	ub_resolve_async(priv->ctx, node, ns_t_a, ns_c_in, lookup_data, host_callback, NULL);
	ub_resolve_async(priv->ctx, node, ns_t_aaaa, ns_c_in, lookup_data, host_callback, NULL);
}

void
start(netresolve_query_t query, char **settings)
{
	const char *node = netresolve_backend_get_nodename(query);
	struct priv_ubdns *priv = netresolve_backend_new_priv(query, sizeof *priv);

	if (!priv)
		goto fail;
	if (!node)
		goto fail;

	priv->ctx = ub_ctx_create();
	if(!priv->ctx)
		goto fail;

	ub_ctx_resolvconf(priv->ctx, NULL);

	start_address_lookup(query);

	netresolve_backend_watch_fd(query, ub_fd(priv->ctx), POLLIN);
	return;
fail:
	netresolve_backend_failed(query);
}

void
dispatch(netresolve_query_t query, int fd, int events)
{
	struct priv_ubdns *priv = netresolve_backend_get_priv(query);

	ub_process(priv->ctx);

	if (priv->finished4 && priv->finished6)
		netresolve_backend_finished(query);
}

void
cleanup(netresolve_query_t query)
{
	struct priv_ubdns *priv = netresolve_backend_get_priv(query);

	if (priv->ctx) {
		netresolve_backend_watch_fd(query, ub_fd(priv->ctx), 0);
		ub_ctx_delete(priv->ctx);
	}
}
