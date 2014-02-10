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
#include <sys/epoll.h>

#include "common.h"

struct event_loop {
	int epoll_fd;
	int epoll_count;
};

static void
watch_fd(netresolve_query_t query, int fd, int events, void *user_data)
{
	struct event_loop *loop = user_data;

	struct epoll_event event = { .events = events, .data = { .fd = fd} };

	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, &event) != -1)
		loop->epoll_count--;
	else if (errno != ENOENT) {
		perror("epoll_ctl");
		abort();
	}
	if (!events)
		return;
	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &event) != -1)
		loop->epoll_count++;
	else {
		perror("epoll_ctl");
		abort();
	}
}

static void
on_success(netresolve_query_t query, void *user_data)
{
	int which = *(int *) netresolve_query_get_user_data(query);
	int *count = user_data;

	switch (which) {
	case 1:
		check_address(query, AF_INET6, "1:2:3:4:5:6:7:8", 999999);
		break;
	case 2:
		check_address(query, AF_INET, "1.2.3.4", 999999);
		break;
	default:
		abort();
	}

	(*count)++;
}

int
main(int argc, char **argv)
{
	struct event_loop loop;
	netresolve_t channel;
	netresolve_query_t query1, query2;
	int finished = 0;
	const char *node1 = "1:2:3:4:5:6:7:8%999999";
	const char *node2 = "1.2.3.4%999999";
	int data1 = 1;
	int data2 = 2;
	const char *service = "80";
	int family = AF_UNSPEC;
	int socktype = 0;
	int protocol = IPPROTO_TCP;

	/* Create epoll file descriptor. */
	loop.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (loop.epoll_fd == -1) {
		perror("epoll_create1");
		abort();
	}
	loop.epoll_count = 0;

	/* Create a channel. */
	channel = netresolve_open();
	if (!channel) {
		perror("netresolve_open");
		abort();
	}

	/* Set callbacks. */
	netresolve_set_fd_callback(channel, watch_fd, &loop);
	netresolve_set_success_callback(channel, on_success, &finished);

	/* Resolver configuration. */
	netresolve_set_family(channel, family);
	netresolve_set_socktype(channel, socktype);
	netresolve_set_protocol(channel, protocol);

	/* Start name resolution. */
	netresolve_set_user_data(channel, &data1);
	query1 = netresolve_query(channel, node1, service);
	netresolve_set_user_data(channel, &data2);
	query2 = netresolve_query(channel, node2, service);

	assert(query1 && query2);
	assert(netresolve_query_get_user_data(query1) == &data1);
	assert(netresolve_query_get_user_data(query2) == &data2);

	/* Run the main loop. */
	while (loop.epoll_count > 0) {
		static const int maxevents = 10;
		struct epoll_event events[maxevents];
		int nevents;
		int i;

		nevents = epoll_wait(loop.epoll_fd, events, maxevents, -1);
		if (nevents == -1) {
			perror("epoll_wait");
			abort();
		}

		for (i = 0; i < nevents; i++)
			netresolve_dispatch_fd(channel, events[i].data.fd, events[i].events);
	}

	assert(finished == 2);

	/* Clean up. */
	netresolve_close(channel);
	close(loop.epoll_fd);

	assert(loop.epoll_count == 0);

	exit(EXIT_SUCCESS);
}
