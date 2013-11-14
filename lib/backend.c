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
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>

#include <netresolve-backend.h>
#include <netresolve-string.h>

#include "netresolve-private.h"

const char *
netresolve_backend_get_node(netresolve_backend_t resolver)
{
	return resolver->request.node;
}

const char *
netresolve_backend_get_service(netresolve_backend_t resolver)
{
	return resolver->request.service;
}

int
netresolve_backend_get_family(netresolve_backend_t resolver)
{
	return resolver->request.family;
}

int
netresolve_backend_get_socktype(netresolve_backend_t resolver)
{
	return resolver->request.socktype;
}

int
netresolve_backend_get_protocol(netresolve_backend_t resolver)
{
	return resolver->request.protocol;
}

bool
netresolve_backend_get_flag(netresolve_backend_t resolver, netresolve_flag_t flag)
{
	return resolver->request.flags & (1 << flag);
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

void
netresolve_backend_add_path(netresolve_backend_t resolver, int family, const void *address, int ifindex, int socktype, int protocol, int portnum)
{
	struct netresolve_response *response = &resolver->response;
	struct netresolve_path path;

	if (resolver->request.family != AF_UNSPEC && resolver->request.family != family)
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

	response->paths = realloc(response->paths, (response->pathcount + 1) * sizeof path);
	memcpy(&response->paths[response->pathcount++], &path, sizeof path);

	debug("added path: %s", netresolve_get_path_string(resolver, response->pathcount - 1));

	if (resolver->callbacks.on_bind)
		_netresolve_bind_path(resolver, &response->paths[response->pathcount - 1]);
}

typedef struct {
	netresolve_backend_t resolver;
	int family;
	const void *address;
	int ifindex;
} PathData;

static void
path_callback(int socktype, int protocol, int port, void *user_data)
{
	PathData *data = user_data;

	netresolve_backend_add_path(data->resolver, data->family, data->address, data->ifindex, socktype, protocol, port);
}

void
netresolve_backend_add_address(netresolve_backend_t resolver, int family, const void *address, int ifindex)
{
	PathData data = { .resolver = resolver, .family = family, .address = address, .ifindex = ifindex };

	_netresolve_get_service_info(path_callback, &data, resolver->request.service,
			resolver->request.socktype, resolver->request.protocol);
}

void
netresolve_backend_set_canonical_name(netresolve_backend_t resolver, const char *canonical_name)
{
	free(resolver->response.canonname);
	resolver->response.canonname = strdup(canonical_name);
}

void *
netresolve_backend_new_priv(netresolve_backend_t resolver, size_t size)
{
	if ((*resolver->backend)->data) {
		error("Backend data already present.");
		free((*resolver->backend)->data);
	}

	(*resolver->backend)->data = calloc(1, size);
	if (!(*resolver->backend)->data)
		netresolve_backend_failed(resolver);

	return (*resolver->backend)->data;
}

void *
netresolve_backend_get_priv(netresolve_backend_t resolver)
{
	return (*resolver->backend)->data;
}

void
netresolve_backend_watch_fd(netresolve_backend_t resolver, int fd, int events)
{
	_netresolve_watch_fd(resolver, fd, events);
}

int
netresolve_backend_watch_timeout(netresolve_backend_t resolver, time_t sec, long nsec)
{
	return _netresolve_add_timeout(resolver, sec, nsec);
}

void
netresolve_backend_drop_timeout(netresolve_backend_t resolver, int fd)
{
	_netresolve_remove_timeout(resolver, fd);
}

static void
backend_cleanup(netresolve_backend_t resolver)
{
	struct netresolve_backend *backend = *resolver->backend;

	if (backend && backend->data) {
		backend->cleanup(resolver);
		free(backend->data);
		backend->data = NULL;
	}
}

void
netresolve_backend_finished(netresolve_backend_t resolver)
{
	if (!*resolver->backend) {
		error("Out of order backend callback.");
		goto fail;
	}

	backend_cleanup(resolver);

	/* Restart with the next *mandatory* backend. */
	while (*++resolver->backend) {
		if ((*resolver->backend)->mandatory) {
			_netresolve_start(resolver);
			return;
		}
	}

	if (resolver->callbacks.on_connect) {
		_netresolve_connect_start(resolver);
		return;
	}

	_netresolve_set_state(resolver, NETRESOLVE_STATE_FINISHED);
	return;

fail:
	_netresolve_set_state(resolver, NETRESOLVE_STATE_FAILED);
}

void
netresolve_backend_failed(netresolve_backend_t resolver)
{
	if (!*resolver->backend) {
		error("Out of order backend callback.");
		goto fail;
	}

	if (resolver->response.pathcount)
		error("Non-empty failed reply.");

	backend_cleanup(resolver);

	/* Restart with the next backend. */
	if (*++resolver->backend) {
		_netresolve_start(resolver);
		return;
	}

fail:
	_netresolve_set_state(resolver, NETRESOLVE_STATE_FAILED);
}

void netresolve_backend_log(netresolve_backend_t resolver, int level, const char *fmt, ...)
{
	if (level <= resolver->log_level) {
		va_list ap;

		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
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
netresolve_backend_apply_hostent(netresolve_backend_t resolver, const struct hostent *he, bool canonname)
{
	char **addr;

	for (addr = he->h_addr_list; *addr; addr++)
		netresolve_backend_add_address(resolver, he->h_addrtype, *addr, 0);

	if (canonname)
		netresolve_backend_set_canonical_name(resolver, he->h_name);
}
