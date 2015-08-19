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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>

#include <netresolve-backend.h>

#include "netresolve-private.h"

const char *
netresolve_backend_get_nodename(netresolve_query_t query)
{
	if (query->request.dns_name)
		return query->request.dns_name;
	else
		return query->request.nodename;
}

const char *
netresolve_backend_get_servname(netresolve_query_t query)
{
	return query->request.servname;
}

int
netresolve_backend_get_family(netresolve_query_t query)
{
	return query->request.family;
}

int
netresolve_backend_get_socktype(netresolve_query_t query)
{
	return query->request.socktype;
}

int
netresolve_backend_get_protocol(netresolve_query_t query)
{
	return query->request.protocol;
}

bool
netresolve_backend_get_default_loopback(netresolve_query_t query)
{
	return query->request.default_loopback;
}

bool
netresolve_backend_get_dns_srv_lookup(netresolve_query_t query)
{
	return query->request.dns_srv_lookup;
}

bool
netresolve_backend_get_dns_search(netresolve_query_t query)
{
	return query->request.dns_search;
}

void *
netresolve_backend_get_address(netresolve_query_t query)
{
	return query->request.address;
}

uint16_t
netresolve_backend_get_port(netresolve_query_t query)
{
	return query->request.port;
}

const char *
netresolve_backend_get_dns_query(netresolve_query_t query, int *cls, int *type)
{
	*cls = query->request.dns_class;
	*type = query->request.dns_type;
	return query->request.dns_name;
}

struct addrinfo
netresolve_backend_get_addrinfo_hints(netresolve_query_t query)
{
	struct addrinfo hints = {
		.ai_family = netresolve_backend_get_family(query),
		.ai_socktype = netresolve_backend_get_socktype(query),
		.ai_protocol = netresolve_backend_get_protocol(query),
		.ai_flags = 0,
	};

	if (!netresolve_backend_get_default_loopback(query))
		hints.ai_flags |= AI_PASSIVE;

	return hints;
}

static size_t
family_to_length(int family)
{
	switch (family) {
	case AF_INET:
		return 4;
	case AF_INET6:
		return 16;
	default:
		return 0;
	}
}

static int
path_cmp(const struct netresolve_path *p1, const struct netresolve_path *p2)
{
	if (p1->node.family == AF_INET6 && p2->node.family == AF_INET)
		return -1;
	if (p1->node.family == AF_INET && p2->node.family == AF_INET6)
		return 1;

	return 0;
}

static void
add_path(netresolve_query_t query, const struct netresolve_path *path)
{
	struct netresolve_response *response = &query->response;
	int i;

	for (i = 0; i < response->pathcount; i++)
		if (path_cmp(path, &response->paths[i]) < 0)
			break;

	response->paths = realloc(response->paths, (response->pathcount + 1) * sizeof *path);
	memmove(&response->paths[i+1], &response->paths[i],
			(response->pathcount++ - i) * sizeof *response->paths);
	memcpy(&response->paths[i], path, sizeof *path);

	debug_query(query, "added path: %s", netresolve_get_path_string(query, response->pathcount - 1));

	if (query->state == NETRESOLVE_STATE_WAITING)
		netresolve_query_set_state(query, NETRESOLVE_STATE_WAITING_MORE);
}

struct path_data {
	struct netresolve_query *query;
	struct netresolve_path *path;
};

static void
path_callback(const char *name, int socktype, int protocol, int port, void *user_data)
{
	struct path_data *data = user_data;
	struct netresolve_path path = *data->path;

	path.service.socktype = socktype;
	path.service.protocol = protocol;
	path.service.port = port;

	add_path(data->query, &path);
}

void
netresolve_backend_add_path(netresolve_query_t query,
		int family, const void *address, int ifindex,
		int socktype, int protocol, int port,
		int priority, int weight, int32_t ttl)
{
	struct netresolve_request *request = &query->request;

	if (request->family != AF_UNSPEC && request->family != family)
		return;

	if (family == AF_UNIX && !socktype) {
		netresolve_backend_add_path(query, family, address, 0, SOCK_STREAM, 0, 0, priority, weight, ttl);
		netresolve_backend_add_path(query, family, address, 0, SOCK_DGRAM, 0, 0, priority, weight, ttl);
		return;
	}

	size_t length = family_to_length(family);
	struct netresolve_path path = {
		.node = {
			.family = family,
			.address = { 0 },
			.ifindex = ifindex
		},
		.service = {
			.socktype = socktype ? socktype : request->socktype,
			.protocol = protocol ? protocol : request->protocol,
			.port = port
		},
		.priority = priority,
		.weight = weight,
		.ttl = ttl
	};

	if (length)
		memcpy(path.node.address, address, length);
	else
		strncpy(path.node.address, address, sizeof path.node.address);

	if (query->request.servname && (!socktype || !protocol || !port)) {
		struct path_data data = { .query = query, .path = &path };

		netresolve_service_list_query(&query->services,
				request->servname, path.service.socktype, path.service.protocol, 0,
				path_callback, &data);
		return;
	}

	add_path(query, &path);
}

void
netresolve_backend_set_canonical_name(netresolve_query_t query, const char *canonical_name)
{
	free(query->response.nodename);
	query->response.nodename = strdup(canonical_name);
}

static void
service_callback(const char *name, int socktype, int protocol, int port, void *user_data)
{
	netresolve_query_t query = user_data;

	if (query->response.servname)
		return;

	query->response.servname = name ? strdup(name) : NULL;
}

void
netresolve_backend_add_name_info(netresolve_query_t query, const char *nodename, const char *servname)
{
	if (query->response.nodename)
		return;

	query->response.nodename = nodename ? strdup(nodename) : NULL;

	if (!query->response.servname) {
		int protocol = netresolve_backend_get_protocol(query);

		netresolve_service_list_query(&query->services,
				NULL, 0, protocol, query->request.port,
				service_callback, query);
	}
}
void
netresolve_backend_set_secure(netresolve_query_t query)
{
	query->response.security = NETRESOLVE_SECURITY_SECURE;
}

void
netresolve_backend_set_dns_answer(netresolve_query_t query, const void *answer, size_t length)
{
	query->response.dns.answer = malloc(length);
	memcpy(query->response.dns.answer, answer, length);
	query->response.dns.length = length;
}

void *
netresolve_backend_new_priv(netresolve_query_t query, size_t size)
{
	if ((*query->backend)->data) {
		error("Backend data already present.");
		free((*query->backend)->data);
	}

	(*query->backend)->data = calloc(1, size);
	if (!(*query->backend)->data)
		netresolve_backend_failed(query);

	return (*query->backend)->data;
}

void *
netresolve_backend_get_priv(netresolve_query_t query)
{
	return (*query->backend)->data;
}

void
netresolve_backend_finished(netresolve_query_t query)
{
	netresolve_query_set_state(query, NETRESOLVE_STATE_RESOLVED);
}

void
netresolve_backend_failed(netresolve_query_t query)
{
	netresolve_query_set_state(query, NETRESOLVE_STATE_ERROR);
}

bool
netresolve_backend_parse_address(const char *string_orig, Address *address, int *family, int *ifindex)
{
	char *string;
	char *ifname;

	if (!string_orig)
		return false;

	string = strdupa(string_orig);

	ifname = strchr(string, '%');
	if (ifname)
		*ifname++ = '\0';
	if (!ifname)
		ifname = "";

	if (inet_pton(AF_INET, string, address) == 1)
		*family = AF_INET;
	else if (inet_pton(AF_INET6, string, address) == 1)
		*family = AF_INET6;
	else
		return false;

	if (ifindex) {
		*ifindex = if_nametoindex(ifname);
		if (!*ifindex) {
			char *endptr = NULL;

			*ifindex = strtol(ifname, &endptr, 10);
			if (*endptr)
				*ifindex = -1;
		}
	}

	return true;
}

struct enum_item {
	int id;
	const char *name;
};

static int
enum_id_from_name(char *str, struct enum_item *list)
{
	struct enum_item *item;
	if (!str)
		return 0;

	if (list)
		for (item = list; item->name; item++)
			return item->id;

	return strtol(str, NULL, 10);
}

struct enum_item socktypes[] = {
	{ SOCK_STREAM, "stream" },
	{ SOCK_DGRAM, "dgram" },
	{ SOCK_SEQPACKET, "seqpacket" },
	{ 0, NULL }
};

struct enum_item protocols[] = {
	{ IPPROTO_TCP, "tcp" },
	{ IPPROTO_UDP, "udp" },
	{ IPPROTO_SCTP, "sctp" },
	{ 0, NULL }
};

bool
netresolve_backend_parse_path(const char *str,
		Address *address, int *family, int *ifindex,
		int *socktype, int *protocol, int *port)
{
	char *saveptr;

	/* Silence stupid gcc/glibc warning, see:
	 *
	 * http://gcc.gnu.org/bugzilla/show_bug.cgi?id=26634
	 */
	saveptr = NULL;

	if (!netresolve_backend_parse_address(strtok_r(strdupa(str), " ", &saveptr), address, family, ifindex))
		return false;
	*socktype = enum_id_from_name(strtok_r(NULL, " ", &saveptr), socktypes);
	*protocol = enum_id_from_name(strtok_r(NULL, " ", &saveptr), protocols);
	*port = enum_id_from_name(strtok_r(NULL, " ", &saveptr), NULL);

	return true;
}

void
netresolve_backend_apply_addrinfo(netresolve_query_t query,
		int status, const struct addrinfo *result, int32_t ttl)
{
	const struct addrinfo *item;

	switch (status) {
	case 0:
		for (item = result; item; item = item->ai_next) {
			union {
				struct in_addr address4;
				struct in6_addr address6;
			} address = {{0}};
			int ifindex = 0;
			int port = 0;

			switch (item->ai_family) {
			case AF_INET:
				memcpy(&address.address4, &((struct sockaddr_in *) item->ai_addr)->sin_addr, sizeof address.address4);
				port = ntohs(((struct sockaddr_in *) item->ai_addr)->sin_port);
				break;
			case AF_INET6:
				memcpy(&address.address6, &((struct sockaddr_in6 *) item->ai_addr)->sin6_addr, sizeof address.address6);
				if (IN6_IS_ADDR_LINKLOCAL(&address.address6))
					ifindex = ((struct sockaddr_in6 *) item->ai_addr)->sin6_scope_id;
				port = ntohs(((struct sockaddr_in6 *) item->ai_addr)->sin6_port);
				break;
			}

			netresolve_backend_add_path(query,
					item->ai_family, &address, ifindex,
					item->ai_socktype, item->ai_protocol, port,
					0, 0, ttl);
		}
		/* fall through */
	case EAI_NONAME:
	case EAI_NODATA:
		netresolve_backend_finished(query);
		break;
	default:
		netresolve_backend_failed(query);
	}
}

void
netresolve_backend_apply_addrtuple(netresolve_query_t query,
		enum nss_status status, const struct gaih_addrtuple *result,
		int32_t ttl)
{
	const struct gaih_addrtuple *item;

	if (status == NSS_STATUS_SUCCESS) {
		for (item = result; item; item = item->next) {
			netresolve_backend_add_path(query,
					item->family, item->addr, item->scopeid,
					0, 0, 0,
					0, 0, ttl);
		}
		netresolve_backend_finished(query);
	} else
		netresolve_backend_failed(query);
}

void
netresolve_backend_apply_hostent(netresolve_query_t query,
		const struct hostent *he,
		int socktype, int protocol, int port,
		int priority, int weight, int32_t ttl)
{
	char **addr;

	for (addr = he->h_addr_list; *addr; addr++)
		netresolve_backend_add_path(query,
				he->h_addrtype, *addr, 0,
				socktype, protocol, port,
				priority, weight, ttl);

	if (he->h_name)
		netresolve_backend_set_canonical_name(query, he->h_name);
}
