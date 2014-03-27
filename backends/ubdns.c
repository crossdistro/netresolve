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
	const uint8_t *address;
	int cls;
	int type;
	struct ub_result *ip4_result;
	struct ub_result *ip6_result;
};

static void lookup(struct priv_ubdns *priv, int family);

static void
callback(void *arg, int status, struct ub_result* result)
{
	struct priv_ubdns *priv = arg;
	netresolve_query_t query = priv->query;

	if (status) {
		error("libunbound: %s\n", ub_strerror(status));
		return;
	}

	if (priv->type) {
		netresolve_backend_set_dns_answer(query, result->answer_packet, result->answer_len);
		netresolve_backend_finished(query);
		return;
	}

	ldns_pkt *pkt;

	if (ldns_wire2pkt(&pkt, result->answer_packet, result->answer_len)) {
		debug("can't parse the DNS answer\n");
		netresolve_backend_failed(query);
		return;
	}

	switch (result->qtype) {
	case ns_t_a:
		debug("received A resonse\n");
		priv->ip4_result = result;
		break;
	case ns_t_aaaa:
		debug("received AAAA resonse\n");
		priv->ip6_result = result;
		break;
	case ns_t_srv:
		/* FIXME: We only support one SRV record per name. */
		debug("received SRV response\n");
		free(priv->node);
		priv->priority = ldns_rdf2native_int16(pkt->_answer->_rrs[0]->_rdata_fields[0]);
		priv->weight = ldns_rdf2native_int16(pkt->_answer->_rrs[0]->_rdata_fields[1]);
		priv->port = ldns_rdf2native_int16(pkt->_answer->_rrs[0]->_rdata_fields[2]);
		priv->node = ldns_rdf2str(pkt->_answer->_rrs[0]->_rdata_fields[3]);
		lookup(priv, AF_INET);
		lookup(priv, AF_INET6);
		break;
	case ns_t_ptr:
		/* FIXME: We only support one PTR record. */
		debug("received PTR response\n");
		if (!pkt->_answer->_rr_count) {
			netresolve_backend_failed(query);
			break;
		}
		char *result = ldns_rdf2str(pkt->_answer->_rrs[0]->_rdata_fields[0]);
		netresolve_backend_set_canonical_name(query, result);
		free(result);
		netresolve_backend_finished(query);
		break;
	default:
		debug("received unknown response\n");
		netresolve_backend_failed(query);
		break;
	}

	ldns_pkt_free(pkt);
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
lookup_reverse(struct priv_ubdns *priv, int family)
{
	char *name;

	switch (family) {
	case AF_INET:
		if (asprintf(&name, "%d.%d.%d.%d.in-addr.arpa.",
				priv->address[3],
				priv->address[2],
				priv->address[1],
				priv->address[0]) == -1) {
			error("memory allocation failed");
			netresolve_backend_failed(priv->query);
			return;
		}
		break;
	default:
		debug("unknown address family: %d", family);
		netresolve_backend_failed(priv->query);
		return;
	}

	debug("looking up PTR record for %s\n", name);
	ub_resolve_async(priv->ctx, name, ns_t_ptr, ns_c_in, priv, callback, NULL);
}

static void
lookup_dns(struct priv_ubdns *priv)
{
	debug("looking up %d record for %s\n", priv->type, priv->node);
	ub_resolve_async(priv->ctx, priv->node, priv->type, priv->cls, priv, callback, NULL);
}

bool
setup(netresolve_query_t query, char **settings)
{
	struct priv_ubdns *priv = netresolve_backend_new_priv(query, sizeof *priv);

	if (!priv)
		goto fail;

	const char *node = netresolve_backend_get_nodename(query);
	priv->node = node ? strdup(node) : NULL;
	priv->family = netresolve_backend_get_family(query);

	priv->query = query;
	priv->ctx = ub_ctx_create();
	if(!priv->ctx)
		goto fail;

	ub_ctx_resolvconf(priv->ctx, NULL);
	netresolve_backend_watch_fd(query, ub_fd(priv->ctx), POLLIN);

	return true;

fail:
	netresolve_backend_failed(query);
	return false;
}

void
setup_forward(netresolve_query_t query, char **settings)
{
	if (!setup(query, settings))
		return;

	struct priv_ubdns *priv = netresolve_backend_get_priv(query);

	if (!priv->node) {
		netresolve_backend_failed(query);
		return;
	}

	if (netresolve_backend_get_dns_srv_lookup(query))
		lookup_srv(priv);
	else {
		lookup(priv, AF_INET);
		lookup(priv, AF_INET6);
	}
}

void
setup_reverse(netresolve_query_t query, char **settings)
{
	if (!setup(query, settings))
		return;

	struct priv_ubdns *priv = netresolve_backend_get_priv(query);
	priv->address = netresolve_backend_get_address(query);

	if (!priv->address) {
		netresolve_backend_failed(query);
		return;
	}

	lookup_reverse(priv, AF_INET);
	//lookup_reverse(priv, AF_INET6);
}

void
setup_dns(netresolve_query_t query, char **settings)
{
	if (!setup(query, settings))
		return;

	struct priv_ubdns *priv = netresolve_backend_get_priv(query);

	if (!priv->node) {
		netresolve_backend_failed(query);
		return;
	}

	netresolve_backend_get_dns_query(query, &priv->cls, &priv->type);

	lookup_dns(priv);
}

static bool
apply_result(netresolve_query_t query, struct ub_result *result)
{
	struct priv_ubdns *priv = netresolve_backend_get_priv(query);
	int family = AF_UNSPEC;

	switch (result->qtype) {
	case ns_t_a:
		family = AF_INET;
		break;
	case ns_t_aaaa:
		family = AF_INET6;
		break;
	}

	if (result->rcode)
		return false;

	for (char **data = result->data; *data; data++)
		netresolve_backend_add_path(query, family, *data, 0,
				0, priv->protocol, priv->port,
				priv->priority, priv->weight, result->ttl);
	return true;
}

void
dispatch(netresolve_query_t query, int fd, int events)
{
	struct priv_ubdns *priv = netresolve_backend_get_priv(query);

	ub_process(priv->ctx);

	if (priv->ip4_result && priv->ip6_result) {
		if (!apply_result(query, priv->ip4_result)) {
			netresolve_backend_failed(query);
			return;
		}
		if (!apply_result(query, priv->ip6_result)) {
			netresolve_backend_failed(query);
			return;
		}
		netresolve_backend_finished(query);
	}
}

void
cleanup(netresolve_query_t query)
{
	struct priv_ubdns *priv = netresolve_backend_get_priv(query);

	if (priv->ip4_result)
		ub_resolve_free(priv->ip4_result);
	if (priv->ip6_result)
		ub_resolve_free(priv->ip6_result);

	if (priv->ctx) {
		netresolve_backend_watch_fd(query, ub_fd(priv->ctx), 0);
		ub_ctx_delete(priv->ctx);
	}
	if (priv->node)
		free(priv->node);
}
