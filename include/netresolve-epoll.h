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
#ifndef NETRESOLVE_EPOLL_H
#define NETRESOLVE_EPOLL_H

#include <netresolve-private.h>
#include <assert.h>
#include <sys/epoll.h>

/* FIXME: This header file should be turned in a library to avoid symbol name clashes. */

struct netresolve_epoll {
	int fd;
	int count;
};

static void *
watch_fd(netresolve_t channel, int fd, int events, void *data, void *user_data)
{
	struct netresolve_epoll *loop = user_data;
	struct epoll_event event = { .events = events, .data = { .ptr = data } };

	assert(fd > 0);
	assert(events == EPOLLIN);
	assert(loop->count == 0);

	if (epoll_ctl(loop->fd, EPOLL_CTL_ADD, fd, &event) == -1) {
		error("epoll_ctl: %s", strerror(errno));
		abort();
	}

	loop->count++;

	return user_data;
}

static void
unwatch_fd(netresolve_t channel, int fd, void *handle, void *user_data)
{
	struct netresolve_epoll *loop = user_data;

	assert(fd > 0);
	assert(handle == user_data);
	assert(loop->count == 1);

	if (epoll_ctl(loop->fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
		error("epoll_ctl: %s", strerror(errno));
		abort();
	}

	loop->count--;
}

__attribute__((unused))
static netresolve_t
netresolve_epoll_open(struct netresolve_epoll *loop)
{
	netresolve_t channel = netresolve_open();
	struct netresolve_fd_callbacks callbacks = { watch_fd, unwatch_fd, loop };

	if (channel)
		netresolve_set_fd_callbacks(channel, &callbacks);

	return channel;
}

__attribute__((unused))
static void
netresolve_epoll_wait(netresolve_t channel, struct netresolve_epoll *loop, bool  block)
{
	while (loop->count > 0) {
		static const int maxevents = 10;
		struct epoll_event events[maxevents];
		int nevents;
		int i;

		nevents = epoll_wait(loop->fd, events, maxevents, block ? -1 : 0);
		switch (nevents) {
		case -1:
			perror("epoll_wait");
			abort();
		case 0:
			return;
		default:
			for (i = 0; i < nevents; i++)
				netresolve_dispatch_fd(channel, events[i].data.ptr, events[i].events);
		}
	}

}

#endif /* NETRESOLVE_EPOLL_H */
