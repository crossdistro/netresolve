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

/* Channel manipulation */
typedef struct netresolve_context *netresolve_t;
typedef struct netresolve_query *netresolve_query_t;

netresolve_t netresolve_open(void);
void netresolve_close(netresolve_t context);

/* Channel configuration */
void netresolve_set_backend_string(netresolve_t context, const char *string);
void netresolve_set_family(netresolve_t context, int family);
void netresolve_set_socktype(netresolve_t context, int socktype);
void netresolve_set_protocol(netresolve_t context, int protocol);
void netresolve_set_default_loopback(netresolve_t context, bool value);
void netresolve_set_dns_srv_lookup(netresolve_t context, bool value);

/* Query constructors and destructors */
netresolve_query_t netresolve_query(netresolve_t context, const char *node, const char *service);
netresolve_query_t netresolve_query_reverse(netresolve_t context, int family, const void *address, int ifindex, int port);
netresolve_query_t netresolve_query_dns(netresolve_t context, const char *dname, int cls, int type);
void netresolve_query_done(netresolve_query_t query);

/* Query callback (nonblocking mode only) */
typedef void (*netresolve_query_callback)(netresolve_query_t query, void *user_data);

void netresolve_query_set_callback(netresolve_query_t query, netresolve_query_callback callback, void *user_data);

/* Query result getters (forward queries) */
size_t netresolve_query_get_count(const netresolve_query_t query);
void netresolve_query_get_node_info(const netresolve_query_t query, size_t idx,
		int *family, const void **address,  int *ifindex);
void netresolve_query_get_service_info(const netresolve_query_t query, size_t idx,
		int *socktype, int *protocol, int *port);
void netresolve_query_get_aux_info(const netresolve_query_t query, size_t idx,
		int *priority, int *weight, int *ttl);
#define netresolve_query_get_canonical_name(query) netresolve_query_get_node_name(query)

/* Query result getters (reverse queries) */
const char *netresolve_query_get_node_name(const netresolve_query_t query);
const char *netresolve_query_get_service_name(const netresolve_query_t query);

/* Query result getters (DNS queries) */
const void *netresolve_query_get_dns_answer(const netresolve_query_t query, size_t *size);

/* Query result getters (universal) */
bool netresolve_query_get_secure(const netresolve_query_t query);

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
