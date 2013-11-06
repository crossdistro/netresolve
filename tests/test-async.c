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
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>

#include <netresolve.h>

void
watch_fd(netresolve_t resolver, int fd, int events, void *user_data)
{
	int epoll_fd = *(int *) user_data;

	struct epoll_event event = { .events = events, .data = { .fd = fd} };

	if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &event) == -1 && errno != ENOENT) {
		perror("epoll_ctl");
		abort();
	}
	if (events && epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
		perror("epoll_ctl");
		abort();
	}
}

void
on_success(netresolve_t resolver, void *user_data)
{
	unsigned char expected_address[16] = { 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8 };
	int family;
	const char *address;
	int ifindex;

	assert(netresolve_get_path_count(resolver) == 1);

	address = netresolve_get_path(resolver, 0, &family, &ifindex, NULL, NULL, NULL);
	assert(family = AF_INET6);
	assert(ifindex == 999999);
	assert(!memcmp(address, expected_address, sizeof expected_address));

	*(bool *) user_data = true;
}

void
on_failure(netresolve_t resolver, void *user_data)
{
	abort();
}

int
main(int argc, char **argv)
{
	netresolve_t resolver;
	int epoll_fd;
	bool finished = false;
	const char *node = "1:2:3:4:5:6:7:8%999999";
	const char *service = "80";
	int family = AF_UNSPEC;
	int socktype = 0;
	int protocol = IPPROTO_TCP;

	/* Create epoll file descriptor. */
	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd == -1) {
		perror("epoll_create1");
		abort();
	}

	/* Create a resolver. */
	resolver = netresolve_open();
	if (!resolver) {
		perror("netresolve_open");
		abort();
	}

	/* Set callbacks. */
	netresolve_callback_set_watch_fd(resolver, watch_fd, &epoll_fd);
	netresolve_callback_set_callbacks(resolver, on_success, on_failure, &finished);

	/* Start name resolution. */
	netresolve_resolve(resolver, node, service, family, socktype, protocol);

	/* Run the main loop. */
	while (!finished) {
		static const int maxevents = 10;
		struct epoll_event events[maxevents];
		int nevents;
		int i;

		nevents = epoll_wait(epoll_fd, events, maxevents, -1);
		if (nevents == -1) {
			perror("epoll_wait");
			abort();
		}

		for (i = 0; i < nevents; i++)
			netresolve_dispatch(resolver, events[i].data.fd, events[i].events);
	}

	/* Clean up. */
	netresolve_close(resolver);
	close(epoll_fd);

	exit(EXIT_SUCCESS);
}
