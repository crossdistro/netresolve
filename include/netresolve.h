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

/* Channels and queries */
typedef struct netresolve_channel *netresolve_t;
typedef struct netresolve_query *netresolve_query_t;

netresolve_t netresolve_open(void);
void netresolve_close(netresolve_t channel);
bool netresolve_dispatch_fd(netresolve_t channel, int fd, int events);

netresolve_query_t netresolve_query(netresolve_t channel, const char *node, const char *service);
netresolve_query_t netresolve_query_reverse(netresolve_t channel, int family, const void *address, int ifindex, int port);
netresolve_query_t netresolve_query_dns(netresolve_t channel, const char *dname, int cls, int type);
void netresolve_query_done(netresolve_query_t query);

/* Callbacks */
typedef void (*netresolve_callback_t)(netresolve_query_t query, void *user_data);
typedef void (*netresolve_fd_callback_t)(netresolve_query_t query, int fd, int events, void *user_data);
typedef void (*netresolve_socket_callback_t)(netresolve_query_t query, int idx, int sock, void *user_data);

void netresolve_set_success_callback(netresolve_t channel,
		netresolve_callback_t on_success, void *user_data);
void netresolve_set_fd_callback(netresolve_t channel,
		netresolve_fd_callback_t watch_fd, void *user_data);
void netresolve_set_bind_callback(netresolve_t channel,
		netresolve_socket_callback_t on_bind, void *user_data);
void netresolve_set_connect_callback(netresolve_t channel,
		netresolve_socket_callback_t on_connect, void *user_data);

/* Configuration */
void netresolve_set_backend_string(netresolve_t channel, const char *string);

/* Input */
void netresolve_set_family(netresolve_t channel, int family);
void netresolve_set_socktype(netresolve_t channel, int socktype);
void netresolve_set_protocol(netresolve_t channel, int protocol);
void netresolve_set_default_loopback(netresolve_t channel, bool value);
void netresolve_set_dns_srv_lookup(netresolve_t channel, bool value);

/* Query user data */
void netresolve_query_set_user_data(netresolve_query_t query, void *user_data);
void *netresolve_query_get_user_data(netresolve_query_t query);

/* Output */
size_t netresolve_query_get_count(const netresolve_query_t query);
const char *netresolve_query_get_node_name(const netresolve_query_t query);
const char *netresolve_query_get_service_name(const netresolve_query_t query);

/* Output: Forward query */
void netresolve_query_get_address_info(const netresolve_query_t query, size_t idx,
		int *family, const void **address,  int *ifindex);
void netresolve_query_get_port_info(const netresolve_query_t query, size_t idx,
		int *socktype, int *protocol, int *port);
void netresolve_query_get_aux_info(const netresolve_query_t query, size_t idx,
		int *priority, int *weight, int *ttl);

/* Output: DNS query */
const void *netresolve_query_get_dns_answer(const netresolve_query_t query, size_t *size);

/* BSD socket API compatibility */
const struct sockaddr *netresolve_query_get_sockaddr(const netresolve_query_t query, size_t idx,
		socklen_t *salen, int *socktype, int *protocol, int32_t *ttl);

/* Logging */
enum netresolve_log_level {
	NETRESOLVE_LOG_LEVEL_QUIET = 0x00,
	NETRESOLVE_LOG_LEVEL_FATAL = 0x10,
	NETRESOLVE_LOG_LEVEL_ERROR = 0x20,
	NETRESOLVE_LOG_LEVEL_INFO = 0x30,
	NETRESOLVE_LOG_LEVEL_DEBUG = 0x40
};
void netresolve_set_log_level(enum netresolve_log_level level);

#endif /* NETRESOLVE_H */
