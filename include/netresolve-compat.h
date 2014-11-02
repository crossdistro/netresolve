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
#ifndef NETRESOLVE_COMPAT_H
#define NETRESOLVE_COMPAT_H

#include <netresolve.h>
#include <netdb.h>

/* Utility functions */
const struct sockaddr *netresolve_query_get_sockaddr(const netresolve_query_t query, size_t idx,
		socklen_t *salen, int *socktype, int *protocol, int32_t *ttl);

/* Functions resembling modern POSIX host/service resolution API */
netresolve_query_t netresolve_query_getaddrinfo(netresolve_t channel,
		const char *node, const char *service, const struct addrinfo *hints);
int netresolve_query_getaddrinfo_done(netresolve_query_t query,
		struct addrinfo **res, int32_t *ttlp);
netresolve_query_t netresolve_query_getnameinfo(netresolve_t channel,
		const struct sockaddr *sa, socklen_t salen, int flags);
int netresolve_query_getnameinfo_done(netresolve_query_t query,
		char **host, char **serv, int32_t *ttlp);

/* Functions resembling obsolete POSIX host/service resolution API */
netresolve_query_t netresolve_query_gethostbyname(netresolve_t channel,
		const char *name, int family);
struct hostent *netresolve_query_gethostbyname_done(netresolve_query_t query,
		int *h_errnop, int32_t *ttlp);
netresolve_query_t netresolve_query_gethostbyaddr(netresolve_t channel,
		const void *address, int length, int family);
struct hostent *netresolve_query_gethostbyaddr_done(netresolve_query_t query,
		int *h_errnop, int32_t *ttlp);

/* Functions resembling POSIX DNS resolver API */
netresolve_query_t netresolve_query_res_query(netresolve_t channel, const char *dname, int cls, int type);
int netresolve_query_res_query_done(netresolve_query_t query, uint8_t **answer);

/* Destructors */
void netresolve_freeaddrinfo(struct addrinfo *ai);
void netresolve_freehostent(struct hostent *he);

#endif /* NETRESOLVE_COMPAT_H */
