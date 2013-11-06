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
#include <sys/timerfd.h>

#include <netresolve-backend.h>

#include "netresolve-private.h"

const char *
netresolve_backend_get_node(netresolve_t resolver)
{
	return resolver->request.node;
}

const char *
netresolve_backend_get_service(netresolve_t resolver)
{
	return resolver->request.service;
}

int
netresolve_backend_get_family(netresolve_t resolver)
{
	return resolver->request.family;
}

int
netresolve_backend_get_socktype(netresolve_t resolver)
{
	return resolver->request.socktype;
}

int
netresolve_backend_get_protocol(netresolve_t resolver)
{
	return resolver->request.protocol;
}

bool
netresolve_backend_get_flag(netresolve_t resolver, netresolve_flag_t flag)
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
netresolve_backend_add_path(netresolve_t resolver, int family, const void *address, int ifindex, int socktype, int protocol, int portnum)
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
}

typedef struct {
	netresolve_t resolver;
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
netresolve_backend_add_address(netresolve_t resolver, int family, const void *address, int ifindex)
{
	PathData data = { .resolver = resolver, .family = family, .address = address, .ifindex = ifindex };

	_netresolve_get_service_info(path_callback, &data, resolver->request.service,
			resolver->request.socktype, resolver->request.protocol);
}

void
netresolve_backend_set_canonical_name(netresolve_t resolver, const char *canonical_name)
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
netresolve_backend_watch_fd(netresolve_t resolver, int fd, int events)
{
	struct epoll_event event = { .events = events, .data = { .fd = fd} };

	if (!resolver->backend || resolver->epoll_fd == -1)
		abort();

	if (epoll_ctl(resolver->epoll_fd, EPOLL_CTL_DEL, fd, &event) == -1 && errno != ENOENT && errno != EBADF)
		error("epoll_ctl: %s", strerror(errno));
	if (events)
		if (epoll_ctl(resolver->epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1)
			error("epoll_ctl: %s", strerror(errno));
}

int
netresolve_backend_watch_timeout(netresolve_backend_t resolver, time_t sec, long nsec)
{
	int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	struct itimerspec timerspec = {{0, 0}, {sec, nsec}};

	if (fd != -1 && timerfd_settime(fd, 0, &timerspec, NULL) == -1) {
		close(fd);
		return -1;
	}

	netresolve_backend_watch_fd(resolver, fd, POLLIN);

	return fd;
}

void
netresolve_backend_drop_timeout(netresolve_backend_t resolver, int fd)
{
	netresolve_backend_watch_fd(resolver, fd, 0);
	close(fd);
}

void
netresolve_backend_finished(netresolve_t resolver)
{
	_netresolve_set_state(resolver, NETRESOLVE_STATE_SUCCESS);
}

void
netresolve_backend_failed(netresolve_t resolver)
{
	_netresolve_set_state(resolver, NETRESOLVE_STATE_FAILURE);
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

void
netresolve_backend_apply_hostent(netresolve_backend_t resolver, const struct hostent *he, bool canonname)
{
	char **addr;

	for (addr = he->h_addr_list; *addr; addr++)
		netresolve_backend_add_address(resolver, he->h_addrtype, *addr, 0);

	if (canonname)
		netresolve_backend_set_canonical_name(resolver, he->h_name);
}
