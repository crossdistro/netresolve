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
#ifndef NETRESOLVE_BACKEND_H
#define NETRESOLVE_BACKEND_H

#include <netresolve-nonblock.h>

#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <nss.h>
#include <poll.h>

typedef struct netresolve_query *netresolve_query_t;
typedef struct netresolve_watch *netresolve_timeout_t;

__attribute__((unused))
static struct in_addr inaddr_any = { 0 };

__attribute__((unused))
#if BYTE_ORDER == BIG_ENDIAN
static const struct in_addr inaddr_loopback = { 0x7f000001 };
#elif BYTE_ORDER == LITTLE_ENDIAN
static const struct in_addr inaddr_loopback = { 0x0100007f };
#else
	#error Neither big endian nor little endian
#endif

/* Input: Forward lookup */
const char *netresolve_backend_get_nodename(netresolve_query_t query);
const char *netresolve_backend_get_servname(netresolve_query_t query);
int netresolve_backend_get_family(netresolve_query_t query);
int netresolve_backend_get_protocol(netresolve_query_t query);
int netresolve_backend_get_socktype(netresolve_query_t query);
bool netresolve_backend_get_default_loopback(netresolve_query_t query);
bool netresolve_backend_get_dns_srv_lookup(netresolve_query_t query);
bool netresolve_backend_get_dns_search(netresolve_query_t query);

/* Input: Reverse lookup */
void *netresolve_backend_get_address(netresolve_query_t query);
uint16_t netresolve_backend_get_port(netresolve_query_t query);

/* Input: DNS record lookup */
const char *netresolve_backend_get_dns_query(netresolve_query_t query, int *cls, int *type);
/* Convenience input */
struct addrinfo netresolve_backend_get_addrinfo_hints(netresolve_query_t query);

/* Output */
void netresolve_backend_add_path(netresolve_query_t query,
		int family, const void *address, int ifindex,
		int socktype, int protocol, int port,
		int priority, int weight, int32_t ttl);
void netresolve_backend_add_name_info(netresolve_query_t query, const char *nodename, const char *servname);
void netresolve_backend_set_canonical_name(netresolve_query_t query, const char *canonical_name);
void netresolve_backend_set_dns_answer(netresolve_query_t query, const void *answer, size_t length);
void netresolve_backend_set_secure(netresolve_query_t query);

/* Convenience output */
void netresolve_backend_apply_addrinfo(netresolve_query_t query,
		int status, const struct addrinfo *result, int32_t ttl);
void netresolve_backend_apply_addrtuple(netresolve_query_t query,
		enum nss_status status, const struct gaih_addrtuple *result,
		int32_t ttl);
void netresolve_backend_apply_hostent(netresolve_query_t query,
		const struct hostent *he,
		int socktype, int protocol, int port,
		int priority, int weight, int32_t ttl);

/* Tools */
typedef void (*netresolve_backend_cleanup_t)(void *priv);

void *netresolve_backend_new_priv(netresolve_query_t query, size_t size, netresolve_backend_cleanup_t cleanup);
void *netresolve_backend_get_priv(netresolve_query_t query);
void netresolve_backend_finished(netresolve_query_t query);
void netresolve_backend_failed(netresolve_query_t query);

/* Events */
typedef void (*netresolve_watch_callback_t)(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data);
typedef void (*netresolve_timeout_callback_t)(netresolve_query_t query, netresolve_timeout_t timeout, void *data);

netresolve_watch_t netresolve_watch_add(netresolve_query_t query, int fd, int events,
		netresolve_watch_callback_t callback, void *data);
void netresolve_watch_remove(netresolve_query_t query, netresolve_watch_t watch, bool do_close);
netresolve_timeout_t netresolve_timeout_add(netresolve_query_t query, time_t sec, long nsec,
		netresolve_timeout_callback_t callback, void *data);
netresolve_timeout_t netresolve_timeout_add_ms(netresolve_query_t query, long msec,
		netresolve_timeout_callback_t callback, void *data);
void netresolve_timeout_remove(netresolve_query_t query, netresolve_timeout_t timeout);

/* Logging */
#define error(...) netresolve_log(0x20, __VA_ARGS__)
#define debug(...) netresolve_log(0x40, __VA_ARGS__)
void netresolve_log(int level, const char *fmt, ...);

/* Convenience */
typedef union { struct in_addr address4; struct in6_addr address6; } Address;
bool netresolve_backend_parse_address(const char *string_orig,
		Address *address, int *family, int *ifindex);
bool netresolve_backend_parse_path(const char *str,
		Address *address, int *family, int *ifindex,
		int *socktype, int *protocol, int *port);

/* Backend function prototypes */
void query_forward(netresolve_query_t query, char **settings);
void query_reverse(netresolve_query_t query, char **settings);
void query_dns(netresolve_query_t query, char **settings);

/* String functions */
const char *netresolve_get_request_string(netresolve_query_t query);
const char *netresolve_get_path_string(netresolve_query_t query, int i);
const char *netresolve_get_response_string(netresolve_query_t query);

#endif /* NETRESOLVE_BACKEND_H */
