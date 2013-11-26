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
void netresolve_set_dns_srv_lookup(netresolve_t resolver, bool value);

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
void netresolve_get_path(const netresolve_t resolver, size_t idx,
		int *family, const void **address, int *ifindex,
		int *socktype, int *protocol, int *port,
		int *priority, int *weight);
const char *netresolve_get_canonical_name(const netresolve_t resolver);
/* Convenient API for use with BSD socket API */
const struct sockaddr *netresolve_get_path_sockaddr(const netresolve_t resolver, size_t n,
		int *socktype, int *protocol, socklen_t *salen);

#endif /* NETRESOLVE_H */
