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

/* Input */
const char *netresolve_backend_get_nodename(netresolve_query_t query);
const char *netresolve_backend_get_servname(netresolve_query_t query);
int netresolve_backend_get_family(netresolve_query_t query);
int netresolve_backend_get_protocol(netresolve_query_t query);
int netresolve_backend_get_socktype(netresolve_query_t query);
bool netresolve_backend_get_default_loopback(netresolve_query_t query);
bool netresolve_backend_get_dns_srv_lookup(netresolve_query_t query);

/* Convenience input */
struct addrinfo netresolve_backend_get_addrinfo_hints(netresolve_query_t query);

/* Output */
void netresolve_backend_add_path(netresolve_query_t query,
		int family, const void *address, int ifindex,
		int socktype, int protocol, int port,
		int priority, int weight, int32_t ttl);
void netresolve_backend_set_canonical_name(netresolve_query_t query, const char *canonical_name);

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
void *netresolve_backend_new_priv(netresolve_query_t query, size_t size);
void *netresolve_backend_get_priv(netresolve_query_t query);
void netresolve_backend_watch_fd(netresolve_query_t query, int fd, int events);
int netresolve_backend_add_timeout(netresolve_query_t query, time_t sec, long nsec);
void netresolve_backend_remove_timeout(netresolve_query_t query, int fd);
void netresolve_backend_finished(netresolve_query_t query);
void netresolve_backend_failed(netresolve_query_t query);

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
void setup(netresolve_query_t query, char **settings);
void dispatch(netresolve_query_t query, int fd, int revents);
void cleanup(netresolve_query_t query);

/* String functions */
const char * netresolve_get_request_string(netresolve_query_t query);
const char * netresolve_get_path_string(netresolve_query_t query, int i);
const char * netresolve_get_response_string(netresolve_query_t query);

#endif /* NETRESOLVE_BACKEND_H */
