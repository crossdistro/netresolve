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
#include <nss.h>
#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <netresolve.h>
#include <netresolve-backend.h>

#include <stdio.h>
#undef error
#undef debug
#define error(...) fprintf(stderr, __VA_ARGS__)
#define debug(...) fprintf(stderr, __VA_ARGS__)

enum netresolve_state {
	NETRESOLVE_STATE_INIT,
	NETRESOLVE_STATE_RESOLVING,
	NETRESOLVE_STATE_FINISHED,
	NETRESOLVE_STATE_FAILED
};

struct netresolve_backend {
	bool mandatory;
	char **settings;
	void *dl_handle;
	void (*start)(netresolve_t resolver, char **settings);
	void (*dispatch)(netresolve_t resolver, int fd, int revents);
	void (*cleanup)(netresolve_t resolver);
	void *data;
};

struct netresolve_path {
	struct {
		int family;
		union {
			char address[1024];
			struct in_addr address4;
			struct in6_addr address6;
		};
		int ifindex;
	} node;
	struct {
		int socktype;
		int protocol;
		int port;
	} service;
};

struct netresolve_resolver {
	int log_level;
	enum netresolve_state state;
	int epoll_fd;
	char *backend_string;
	struct netresolve_backend **backends, **backend;
	struct {
		netresolve_callback_t on_success;
		netresolve_callback_t on_failure;
		void *user_data;
		netresolve_fd_callback_t watch_fd;
		void *user_data_fd;
		netresolve_socket_callback_t on_bind;
		netresolve_socket_callback_t on_connect;
		void *user_data_sock;
	} callbacks;
	struct netresolve_request {
		/* Perform L3 address resolution using 'node' if not NULL. Use
		 * 'family' to chose between IPv4, IPv6 and mixed IPv4/IPv6
		 * resolution and additional flags to further tweak node name
		 * resolution.
		 */
		const char *node;
		int family;
		/* Perform L4 port resolution using 'service' if not NULL. Use
		 * 'socktype' and 'protocol' to limit the possible options and
		 * additional flags to further tweak service name resolution.
		 */
		const char *service;
		int socktype;
		int protocol;
		/* See `netresolve-common.h` for details. */
		int flags;
	} request;
	struct netresolve_response {
		struct netresolve_path *paths;
		size_t pathcount;
		char *canonname;
	} response;

	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
	} sa_buffer;
	char buffer[1024];
};

void _netresolve_set_state(netresolve_t resolver, enum netresolve_state state);

void _netresolve_start(netresolve_t resolver);
void _netresolve_epoll(netresolve_t resolver, int timeout);
void _netresolve_watch_fd(netresolve_t resolver, int fd, int events);

void _netresolve_backend_cleanup(netresolve_t resolver);

void _netresolve_bind(netresolve_t resolver);
void _netresolve_connect(netresolve_t resolver);

void _netresolve_get_service_info(void (*callback)(int, int, int, void *), void *user_data,
		const char *request_service, int socktype, int protocol);
