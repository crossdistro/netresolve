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
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#include <netresolve.h>
#include <netresolve-cli.h>

static void
read_and_write(int rfd, int wfd)
{
	char buffer[1024];
	ssize_t rsize, wsize, offset;

	rsize = read(rfd, buffer, sizeof(buffer));
	if (rsize == -1) {
		fprintf(stderr, "read: %s\n", strerror(errno));
		abort();
	}
	if (rsize == 0) {
		if (rfd == 0)
			return;
		else {
			fprintf(stderr, "end of input\n");
			abort();
		}
	}
	for (offset = 0; offset < rsize; offset += wsize) {
		fprintf(stderr, "%s: <<<%*s>>>\n",
				(rfd == 0) ? "sending" : "receiving",
				(int) (rsize - offset), buffer + offset);
		wsize = write(wfd, buffer + offset, rsize - offset);
		if (wsize <= 0) {
			fprintf(stderr, "write: %s\n", strerror(errno));
			abort();
		}
	}
}

static void
on_connect(netresolve_query_t channel, int idx, int sock, void *user_data)
{
	*(int *) user_data = sock;
}

int
main(int argc, char **argv)
{
	netresolve_t channel;
	netresolve_query_t query;
	int sock = -1;
	struct pollfd fds[2];

	channel = netresolve_open();
	if (!channel) {
		fprintf(stderr, "netresolve: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	netresolve_set_connect_callback(channel, on_connect, &sock);

	query = netresolve_query_argv(channel, argv);
	if (!query) {
		fprintf(stderr, "netresolve: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	if (sock == -1) {
		fprintf(stderr, "no connection\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Connected.\n");

	fds[0].fd = 0;
	fds[0].events = POLLIN;
	fds[1].fd = sock;
	fds[1].events = POLLIN;

	while (true) {
		if (poll(fds, 2, -1) == -1) {
			fprintf(stderr, "poll: %s\n", strerror(errno));
			break;
		}

		if (fds[0].revents & POLLIN)
			read_and_write(0, sock);
		if (fds[1].revents & POLLIN)
			read_and_write(sock, 1);
	}

	netresolve_close(channel);
	return EXIT_SUCCESS;
}
