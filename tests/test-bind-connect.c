/* Copyright (c) 2013+ Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
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
#include <netresolve-socket.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

static const char outbuf[6] = "asdf\n";

struct socket {
	int count;
};

static void
on_connect(netresolve_query_t query, int idx, int fd, void *user_data)
{
	struct socket *sock = user_data;
	int status;

	sock->count++;

	/* Send and close */
	status = send(fd, outbuf, strlen(outbuf), 0);
	assert(status == strlen(outbuf));
	status = shutdown(fd, SHUT_RDWR);
	assert(status == 0);
	status = close(fd);
	assert(status == 0);

	/* Request next connected socket */
	netresolve_connect_next(query);
}

static void
on_accept(netresolve_query_t query, int idx, int fd, void *user_data)
{
	struct socket *sock = user_data;
	int status;
	char inbuf[16] = {0};

	sock->count++;

	/* Receive and close */
	status = recv(fd, inbuf, sizeof inbuf, 0);
	assert(status == strlen(outbuf));
	status = shutdown(fd, SHUT_RDWR);
	assert(status == 0);
	status = close(fd);
	assert(status == 0);

	/* Check */
	assert(!strcmp(inbuf, outbuf));

	/* Accept only two connections */
	if (sock->count == 2)
		netresolve_listen_free(query);
}

int
main(int argc, char **argv)
{
	const char *node = NULL;
	const char *service = "1024";
	int family = AF_UNSPEC;
	int socktype = SOCK_STREAM;
	int protocol = IPPROTO_TCP;
	struct socket server = {};
	struct socket client = {};

	netresolve_query_t query_server, query_client;

	/* Start listening */
	query_server = netresolve_listen(NULL, node, service, family, socktype, protocol);
	assert(query_server);

	/* Connect */
	query_client = netresolve_connect(NULL, node, service, family, socktype, protocol, on_connect, &client);
	assert(query_server);
	netresolve_connect_free(query_client);
	assert(client.count == 2);

	/* Accept */
	netresolve_accept(query_server, on_accept, &server);
	assert(server.count == 2);

	return EXIT_SUCCESS;
}
