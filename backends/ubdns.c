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
#include <ldns/ldns.h>

struct priv_ubdns {
	netresolve_query_t query;
	struct ub_ctx* ctx;
	int protocol;
	uint16_t port;
	int priority;
	int weight;
	char *node;
	int family;
	bool ip4_finished;
	bool ip6_finished;
};

static void lookup(struct priv_ubdns *priv, int family);

static void
callback(void *arg, int status, struct ub_result* result)
{
	struct priv_ubdns *priv = arg;
	netresolve_query_t query = priv->query;
	int family = AF_UNSPEC;

	if (status) {
		error("libunbound: %s\n", ub_strerror(status));
		return;
	}

	switch (result->qtype) {
	case ns_t_srv:
		debug("received SRV response\n");
		break;
	case ns_t_a:
		debug("received A resonse\n");
		priv->ip4_finished = true;
		family = AF_INET;
		break;
	case ns_t_aaaa:
		debug("received AAAA resonse\n");
		priv->ip6_finished = true;
		family = AF_INET6;
		break;
	}

	if (family)
		for (char **data = result->data; *data; data++)
			if (priv->port)
				netresolve_backend_add_path(query, family, *data, 0,
						0, priv->protocol, priv->port,
						priv->priority, priv->weight, result->ttl);
			else
				netresolve_backend_add_path(query, family, *data, 0,
						0, 0, 0,
						priv->priority, priv->weight, result->ttl);
	else {
		ldns_pkt *pkt;
		ldns_status status = ldns_wire2pkt(&pkt, result->answer_packet, result->answer_len);

		if (status) {
			netresolve_backend_failed(query);
			return;
		}

		/* FIXME: We only support one SRV record per name. */
		free(priv->node);
		priv->priority = ldns_rdf2native_int16(pkt->_answer->_rrs[0]->_rdata_fields[0]);
		priv->weight = ldns_rdf2native_int16(pkt->_answer->_rrs[0]->_rdata_fields[1]);
		priv->port = ldns_rdf2native_int16(pkt->_answer->_rrs[0]->_rdata_fields[2]);
		priv->node = ldns_rdf2str(pkt->_answer->_rrs[0]->_rdata_fields[3]);
		lookup(priv, AF_INET);
		lookup(priv, AF_INET6);

		ldns_pkt_free(pkt);
	}

	ub_resolve_free(result);
}

static const char *
protocol_to_string(int proto)
{
	switch (proto) {
	case IPPROTO_UDP:
		return "udp";
	case IPPROTO_TCP:
		return "tcp";
	case IPPROTO_SCTP:
		return "sctp";
	default:
		return "0";
	}
}

static void
lookup_srv(struct priv_ubdns *priv)
{
	char *name;

	priv->protocol = netresolve_backend_get_protocol(priv->query);

	if (asprintf(&name, "_%s._%s.%s",
			netresolve_backend_get_servname(priv->query),
			protocol_to_string(priv->protocol),
			netresolve_backend_get_nodename(priv->query)) == -1) {
		error("memory allocation failed");
		netresolve_backend_failed(priv->query);
		return;
	}
	debug("looking up SRV record for %s\n", name);
	ub_resolve_async(priv->ctx, name, ns_t_srv, ns_c_in, priv, callback, NULL);
}

static void
lookup(struct priv_ubdns *priv, int family)
{
	int type;
	const char *type_name;

	if (priv->family != AF_UNSPEC && priv->family != family)
		return;

	switch (family) {
	case AF_INET:
		type = ns_t_a;
		type_name = "A";
		break;
	case AF_INET6:
		type = ns_t_aaaa;
		type_name = "AAAA";
		break;
	default:
		return;
	}

	debug("looking up %s record for %s\n", type_name, priv->node);
	ub_resolve_async(priv->ctx, priv->node, type, ns_c_in, priv, callback, NULL);
}

void
setup_forward(netresolve_query_t query, char **settings)
{
	struct priv_ubdns *priv = netresolve_backend_new_priv(query, sizeof *priv);
	const char *node = netresolve_backend_get_nodename(query);

	if (!priv || !node)
		goto fail;

	priv->query = query;
	priv->node = strdup(node);
	priv->family = netresolve_backend_get_family(query);

	if (!priv->node)
		goto fail;

	priv->node = strdup(priv->node);

	priv->ctx = ub_ctx_create();
	if(!priv->ctx)
		goto fail;

	ub_ctx_resolvconf(priv->ctx, NULL);
	netresolve_backend_watch_fd(query, ub_fd(priv->ctx), POLLIN);

	if (netresolve_backend_get_dns_srv_lookup(query))
		lookup_srv(priv);
	else {
		lookup(priv, AF_INET);
		lookup(priv, AF_INET6);
	}

	return;

fail:
	netresolve_backend_failed(query);
}

void
dispatch(netresolve_query_t query, int fd, int events)
{
	struct priv_ubdns *priv = netresolve_backend_get_priv(query);

	ub_process(priv->ctx);

	if (priv->ip4_finished && priv->ip6_finished)
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
	if (priv->node)
		free(priv->node);
}
