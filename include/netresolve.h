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
#ifndef NETRESOLVE_H
#define NETRESOLVE_H

#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <netresolve-common.h>

typedef struct netresolve_resolver *netresolve_t;

/* Open/close resolver instance */
netresolve_t netresolve_open();
void netresolve_close(netresolve_t resolver);

/* Configuration API */
void netresolve_set_log_level(netresolve_t resolver, int level);
void netresolve_set_backend_string(netresolve_t resolver, const char *string);
void netresolve_set_default_loopback(netresolve_t resolver, bool value);

/* Callback API */
typedef void (*netresolve_callback_t)(netresolve_t resolver, void *user_data);
void netresolve_callback_set_callbacks(netresolve_t resolver,
		netresolve_callback_t on_success,
		netresolve_callback_t on_failure,
		void *user_data);
typedef void (*netresolve_fd_callback_t)(netresolve_t resolver, int fd, int events, void *user_data);
void netresolve_callback_set_watch_fd(netresolve_t resolver,
		netresolve_fd_callback_t watch_fd,
		void *user_data);
typedef void (*netresolve_socket_callback_t)(netresolve_t resolver, int sock, void *user_data);
void netresolve_callback_set_bind(netresolve_t resolver,
		netresolve_socket_callback_t on_bind,
		void *user_data);
void netresolve_callback_set_connect(netresolve_t resolver,
		netresolve_socket_callback_t on_connect,
		void *user_data);

/* Request API */
int netresolve_resolve(netresolve_t resolver,
		const char *node, const char *service, int family, int socktype, int protocol);
int netresolve_dispatch(netresolve_t resolver, int fd, int events);

/* Response API */
size_t netresolve_get_path_count(const netresolve_t resolver);
const void *netresolve_get_path(const netresolve_t resolver, size_t idx,
		int *family, int *ifindex, int *socktype, int *protocol, int *port);
const char *netresolve_get_canonical_name(const netresolve_t resolver);
/* Convenient API for use with BSD socket API */
const struct sockaddr *netresolve_get_path_sockaddr(const netresolve_t resolver, size_t n,
		int *socktype, int *protocol, socklen_t *salen);

#endif /* NETRESOLVE_H */
