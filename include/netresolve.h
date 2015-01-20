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

typedef struct netresolve_context *netresolve_t;
typedef struct netresolve_query *netresolve_query_t;

/* Configuration options */
enum netresolve_option {
	NETRESOLVE_OPTION_DONE,
/* Flags:
 *
 * NETRESOLVE_OPTION_DEFAULT_LOOPBACK:
 *  - When set, NULL resolves to a loopback address, otherwise it resolves
 *    to an empty address. The opposite of getaddrinfo's AI_PASSIVE.
 * NETRESOLVE_OPTION_DNS_SRV_LOOKUP:
 *  - When set, forward lookups use DNS SRV records when applicable.
 */
	NETRESOLVE_OPTION_DEFAULT_LOOPBACK = 0x10, /* bool default_loopback */
	NETRESOLVE_OPTION_DNS_SRV_LOOKUP, /* bool dns_srv_lookup */
/* Node and service name:
 *
 * You don't normally need to set them as they are specified as parameters
 * to the `netresolve_query_forward()` function.
 */
	NETRESOLVE_OPTION_NODE_NAME = 0x100, /* const char *nodename */
	NETRESOLVE_OPTION_SERVICE_NAME, /* const char *servname */
/* Family, socktype and protocol:
 *
 * You can set them to adjust forward name resolution configuration before
 * calling the `netresolve_query_forward()` function.
 */
	NETRESOLVE_OPTION_FAMILY = 0x110, /* int family */
	NETRESOLVE_OPTION_SOCKTYPE, /* int socktype */
	NETRESOLVE_OPTION_PROTOCOL, /* int protocol */
/* IPv4 or IPv6 address, port number:
 *
 * You don't normally need to set them as they are specified as parameters
 * to the `netresolve_query_reverse()` function.
 */
	NETRESOLVE_OPTION_IFINDEX = 0x200, /* int ifindex */
	NETRESOLVE_OPTION_IP4_ADDRESS = 0x210, /* const struct in_addr *address */
	NETRESOLVE_OPTION_IP6_ADDRESS, /* const struct in6_addr *address */
	NETRESOLVE_OPTION_PORT = 0x220, /* int port */
/* DNS record resolution:
 *
 * You don't normally need to set them as they are specified as parameters
 * to the `netresolve_query_dns()` function.
 */
	NETRESOLVE_OPTION_DNS_NAME = 0x300, /* const char *dname */
	NETRESOLVE_OPTION_DNS_CLASS, /* int class */
	NETRESOLVE_OPTION_DNS_TYPE, /* int type */
};

/* Context construction and destruction */
netresolve_t netresolve_context_new(void);
void netresolve_context_free(netresolve_t context);

/* Context configuration */
void netresolve_set_backend_string(netresolve_t context, const char *string);
void netresolve_context_set_options(netresolve_t context, ...);

/* Query construction and destruction */
typedef void (*netresolve_query_callback)(netresolve_query_t query, void *user_data);

netresolve_query_t netresolve_query_forward(netresolve_t context,
		const char *node, const char *service,
		netresolve_query_callback callback, void *user_data);
netresolve_query_t netresolve_query_reverse(netresolve_t context,
		int family, const void *address, int ifindex, int protocol, int port,
		netresolve_query_callback callback, void *user_data);
netresolve_query_t netresolve_query_dns(netresolve_t context,
		const char *dname, int cls, int type,
		netresolve_query_callback callback, void *user_data);
void netresolve_query_free(netresolve_query_t query);

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
