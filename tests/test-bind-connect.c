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
#include <netresolve-socket.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>


static void
on_socket(netresolve_query_t query, int idx, int sock, void *user_data)
{
	int *psock = user_data;

	if (*psock == -1)
		*psock = sock;
	else
		close(sock);
}

int
do_bind(const char *node, const char *service, int family, int socktype, int protocol)
{
	int sock = -1;

	netresolve_bind(NULL, node, service, family, socktype, protocol, on_socket, &sock);

	return sock;
}

int
do_connect(const char *node, const char *service, int family, int socktype, int protocol)
{
	int sock = -1;

	netresolve_connect(NULL, node, service, family, socktype, protocol, on_socket, &sock);

	return sock;
}
int
main(int argc, char **argv)
{
	int sock_server, sock_client, sock_accept;
	const char *node = NULL;
	const char *service = "1024";
	int family = AF_INET;
	int socktype = SOCK_STREAM;
	int protocol = IPPROTO_TCP;
	int status;
	char outbuf[6] = "asdf\n";
	char inbuf[6] = {0};

	sock_server = do_bind(node, service, family, socktype, protocol);
	assert(sock_server > 0);
	status = listen(sock_server, 10);
	assert(status == 0);

	sock_client = do_connect(node, service, family, socktype, protocol);
	assert(sock_client > 0);

	sock_accept = accept(sock_server, NULL, 0);
	assert(sock_accept != -1);
	status = send(sock_client, outbuf, strlen(outbuf), 0);
	assert(status == strlen(outbuf));
	status = recv(sock_accept, inbuf, sizeof inbuf, 0);
	assert(status == strlen(outbuf));
	assert(!strcmp(inbuf, outbuf));

	status = close(sock_server);
	assert(status == 0);
	status = close(sock_client);
	assert(status == 0);

	return EXIT_SUCCESS;
}
