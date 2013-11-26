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
	const void *address;
	int ifindex;

	assert(netresolve_get_path_count(resolver) == 1);

	netresolve_get_path(resolver, 0, &family, &address, &ifindex, NULL, NULL, NULL, NULL, NULL);
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
