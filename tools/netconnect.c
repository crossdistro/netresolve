/* This file is part of the `netresolve` library.
 * Copyright (C) Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#include <netresolve.h>
#include <netresolve-cli.h>
#include <netresolve-string.h>

void
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

void
on_connect(netresolve_t resolver, int sock, void *user_data)
{
	*(int *) user_data = sock;
}

int
main(int argc, char **argv)
{
	netresolve_t resolver;
	int status;
	int sock = -1;
	struct pollfd fds[2];

	resolver = netresolve_open();
	if (!resolver) {
		fprintf(stderr, "netresolve: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	netresolve_callback_set_connect(resolver, on_connect, &sock);

	status = netresolve_resolve_argv(resolver, argv + 1);
	if (status) {
		fprintf(stderr, "netresolve: %s\n", strerror(status));
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

	netresolve_close(resolver);
	return EXIT_SUCCESS;
}
