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
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>

#include <netresolve-backend.h>
#include <netresolve-string.h>

#include "netresolve-private.h"

const char *
netresolve_backend_get_node(netresolve_query_t channel)
{
	return channel->request.node;
}

const char *
netresolve_backend_get_service(netresolve_query_t channel)
{
	return channel->request.service;
}

int
netresolve_backend_get_family(netresolve_query_t channel)
{
	return channel->request.family;
}

int
netresolve_backend_get_socktype(netresolve_query_t channel)
{
	return channel->request.socktype;
}

int
netresolve_backend_get_protocol(netresolve_query_t channel)
{
	return channel->request.protocol;
}

bool
netresolve_backend_get_default_loopback(netresolve_query_t channel)
{
	return channel->request.default_loopback;
}

bool
netresolve_backend_get_dns_srv_lookup(netresolve_query_t channel)
{
	return channel->request.dns_srv_lookup;
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

typedef struct {
	netresolve_query_t channel;
	int family;
	const void *address;
	int ifindex;
} PathData;

static void
path_callback(int socktype, int protocol, int port, void *user_data)
{
	PathData *data = user_data;

	netresolve_backend_add_path(data->channel,
			data->family, data->address, data->ifindex,
			socktype, protocol, port,
			0, 0);
}

static int
path_cmp(struct netresolve_path *p1, struct netresolve_path *p2)
{
	if (p1->node.family == AF_INET6 && p2->node.family == AF_INET)
		return -1;
	if (p1->node.family == AF_INET && p2->node.family == AF_INET6)
		return 1;

	return 0;
}

void
netresolve_backend_add_path(netresolve_query_t channel,
		int family, const void *address, int ifindex,
		int socktype, int protocol, int portnum,
		int priority, int weight)
{
	struct netresolve_response *response = &channel->response;
	struct netresolve_path path;
	int i;

	if (family == AF_UNIX && socktype == -1) {
		netresolve_backend_add_path(channel, family, address, 0, SOCK_STREAM, 0, 0, 0, 0);
		netresolve_backend_add_path(channel, family, address, 0, SOCK_DGRAM, 0, 0, 0, 0);
		return;
	}

	if (socktype == -1 || protocol == -1 || portnum == -1) {
		PathData data = {
			.channel = channel,
			.family = family,
			.address = address,
			.ifindex = ifindex,
		};

		netresolve_get_service_info(path_callback, &data, channel->request.service,
				channel->request.socktype, channel->request.protocol);
		return;
	}

	if (channel->request.family != AF_UNSPEC && channel->request.family != family)
		return;

	size_t length = family_to_length(family);

	memset(&path, 0, sizeof path);
	path.node.family = family;
	if (length)
		memcpy(path.node.address, address, length);
	else
		strncpy(path.node.address, address, sizeof path.node.address);
	path.node.ifindex = ifindex;
	path.service.socktype = socktype;
	path.service.protocol = protocol;
	path.service.port = portnum;
	path.priority = priority;
	path.weight = weight;

	for (i = 0; i < response->pathcount; i++)
		if (path_cmp(&path, &response->paths[i]) < 0)
			break;

	response->paths = realloc(response->paths, (response->pathcount + 1) * sizeof path);
	memmove(&response->paths[i+1], &response->paths[i],
			(response->pathcount++ - i) * sizeof *response->paths);
	memcpy(&response->paths[i], &path, sizeof path);

	debug("added path: %s", netresolve_get_path_string(channel, response->pathcount - 1));

	if (channel->callbacks.on_bind)
		netresolve_bind_path(channel, &response->paths[response->pathcount - 1]);
}

void
netresolve_backend_set_canonical_name(netresolve_query_t channel, const char *canonical_name)
{
	free(channel->response.canonname);
	channel->response.canonname = strdup(canonical_name);
}

void *
netresolve_backend_new_priv(netresolve_query_t channel, size_t size)
{
	if ((*channel->backend)->data) {
		error("Backend data already present.\n");
		free((*channel->backend)->data);
	}

	(*channel->backend)->data = calloc(1, size);
	if (!(*channel->backend)->data)
		netresolve_backend_failed(channel);

	return (*channel->backend)->data;
}

void *
netresolve_backend_get_priv(netresolve_query_t channel)
{
	return (*channel->backend)->data;
}

void
netresolve_backend_watch_fd(netresolve_query_t channel, int fd, int events)
{
	netresolve_watch_fd(channel, fd, events);
}

int
netresolve_backend_watch_timeout(netresolve_query_t channel, time_t sec, long nsec)
{
	return netresolve_add_timeout(channel, sec, nsec);
}

void
netresolve_backend_drop_timeout(netresolve_query_t channel, int fd)
{
	netresolve_remove_timeout(channel, fd);
}

static void
backend_cleanup(netresolve_query_t channel)
{
	struct netresolve_backend *backend = *channel->backend;

	if (backend && backend->data) {
		if (backend->cleanup)
			backend->cleanup(channel);
		free(backend->data);
		backend->data = NULL;
	}
}

void
netresolve_backend_finished(netresolve_query_t channel)
{
	if (!*channel->backend) {
		error("Out of order backend callback.");
		goto fail;
	}

	backend_cleanup(channel);

	/* Restart with the next *mandatory* backend. */
	while (*++channel->backend) {
		if ((*channel->backend)->mandatory) {
			netresolve_start(channel);
			return;
		}
	}

	if (channel->callbacks.on_connect) {
		netresolve_connect_start(channel);
		return;
	}

	netresolve_set_state(channel, NETRESOLVE_STATE_FINISHED);
	return;

fail:
	netresolve_set_state(channel, NETRESOLVE_STATE_FAILED);
}

void
netresolve_backend_failed(netresolve_query_t channel)
{
	if (!*channel->backend) {
		error("Out of order backend callback.");
		goto fail;
	}

	if (channel->response.pathcount)
		error("Non-empty failed reply.");

	backend_cleanup(channel);

	/* Restart with the next backend. */
	if (*++channel->backend) {
		netresolve_start(channel);
		return;
	}

fail:
	netresolve_set_state(channel, NETRESOLVE_STATE_FAILED);
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
	if (ifindex) {
		*ifindex = if_nametoindex(ifname);
		if (!*ifindex) {
			char *endptr = NULL;

			*ifindex = strtol(ifname, &endptr, 10);
			if (*endptr)
				return false;
		}
	}

	if (inet_pton(AF_INET, string, address) == 1) {
		*family = AF_INET;
		return true;
	}
	if (inet_pton(AF_INET6, string, address) == 1) {
		*family = AF_INET6;
		return true;
	}

	return false;
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
	{ -1, NULL }
};

struct enum_item protocols[] = {
	{ IPPROTO_TCP, "tcp" },
	{ IPPROTO_UDP, "udp" },
	{ IPPROTO_SCTP, "sctp" },
	{ -1, NULL }
};

bool
netresolve_backend_parse_path(const char *str,
		Address *address, int *family, int *ifindex,
		int *socktype, int *protocol, int *port)
{
	char *saveptr;

	if (!netresolve_backend_parse_address(strtok_r(strdupa(str), " ", &saveptr), address, family, ifindex))
		return false;
	*socktype = enum_id_from_name(strtok_r(NULL, " ", &saveptr), socktypes);
	*protocol = enum_id_from_name(strtok_r(NULL, " ", &saveptr), protocols);
	*port = enum_id_from_name(strtok_r(NULL, " ", &saveptr), NULL);

	return true;
}

void
netresolve_backend_apply_hostent(netresolve_query_t channel,
		const struct hostent *he,
		int socktype, int protocol, int port,
		int priority, int weight)
{
	char **addr;

	for (addr = he->h_addr_list; *addr; addr++)
		netresolve_backend_add_path(channel,
				he->h_addrtype, *addr, 0,
				socktype, protocol, port,
				priority, weight);

	if (he->h_name)
		netresolve_backend_set_canonical_name(channel, he->h_name);
}
