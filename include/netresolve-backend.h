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
#include <poll.h>

#include <netresolve-common.h>

typedef struct netresolve_resolver *netresolve_backend_t;

/* Input */
const char *netresolve_backend_get_node(netresolve_backend_t resolver);
const char *netresolve_backend_get_service(netresolve_backend_t resolver);
int netresolve_backend_get_family(netresolve_backend_t resolver);
int netresolve_backend_get_protocol(netresolve_backend_t resolver);
int netresolve_backend_get_socktype(netresolve_backend_t resolver);
bool netresolve_backend_get_default_loopback(netresolve_backend_t resolver);
bool netresolve_backend_get_dns_srv_lookup(netresolve_backend_t resolver);

/* Output */
void netresolve_backend_add_path(netresolve_backend_t resolver,
		int family, const void *address, int ifindex,
		int socktype, int protocol, int port,
		int priority, int weight);
#define netresolve_backend_add_address(resolver, family, address, ifindex) \
	netresolve_backend_add_path(resolver, family, address, ifindex, -1, -1, -1, 0, 0);
void netresolve_backend_set_canonical_name(netresolve_backend_t resolver, const char *canonical_name);

/* Tools */
void *netresolve_backend_new_priv(netresolve_backend_t resolver, size_t size);
void *netresolve_backend_get_priv(netresolve_backend_t resolver);
void netresolve_backend_watch_fd(netresolve_backend_t resolver, int fd, int events);
int netresolve_backend_watch_timeout(netresolve_backend_t resolver, time_t sec, long nsec);
void netresolve_backend_drop_timeout(netresolve_backend_t resolver, int fd);
void netresolve_backend_finished(netresolve_backend_t resolver);
void netresolve_backend_failed(netresolve_backend_t resolver);

/* Logging */
#define error(...) netresolve_backend_log(resolver, 1, __VA_ARGS__)
#define debug(...) netresolve_backend_log(resolver, 2, __VA_ARGS__)
void netresolve_backend_log(netresolve_backend_t resolver, int level, const char *fmt, ...);

/* Convenience */
typedef union { struct in_addr address4; struct in6_addr address6; } Address;
bool netresolve_backend_parse_address(const char *string_orig,
		Address *address, int *family, int *ifindex);
bool netresolve_backend_parse_path(const char *str,
		Address *address, int *family, int *ifindex,
		int *socktype, int *protocol, int *port);
void netresolve_backend_apply_hostent(netresolve_backend_t resolver,
		const struct hostent *he,
		int socktype, int protocol, int port,
		int priority, int weight);

/* Backend function prototypes */
void start(netresolve_backend_t resolver, char **settings);
void dispatch(netresolve_backend_t resolver, int fd, int revents);
void cleanup(netresolve_backend_t resolver);

#endif /* NETRESOLVE_BACKEND_H */
