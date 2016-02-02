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
#ifndef NETRESOLVE_SOCKET_H
#define NETRESOLVE_SOCKET_H

#include <netresolve.h>

typedef void (*netresolve_socket_callback_t)(netresolve_query_t query, int idx, int sock, void *user_data);

netresolve_query_t netresolve_connect(netresolve_t context,
		const char *nodename, const char *servname,
		int family, int socktype, int protocol,
		netresolve_socket_callback_t callback, void *user_data);
void netresolve_connect_next(netresolve_query_t query);
void netresolve_connect_free(netresolve_query_t query);

netresolve_query_t netresolve_listen(netresolve_t context,
		const char *nodename, const char *servname,
		int family, int socktype, int protocol);
void netresolve_accept(netresolve_query_t query, netresolve_socket_callback_t on_accept, void *user_data);
void netresolve_listen_free(netresolve_query_t query);

#endif /* NETRESOLVE_SOCKET_H */
