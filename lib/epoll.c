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
#include <netresolve-epoll.h>
#include <netresolve-private.h>
#include <unistd.h>
#include <assert.h>

static void *
watch_fd(netresolve_t channel, int fd, int events, void *data)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(channel);
	struct epoll_event event = { .events = events, .data = { .ptr = data } };

	if (epoll_ctl(loop->fd, EPOLL_CTL_ADD, fd, &event) == -1) {
		error("epoll_ctl: %s", strerror(errno));
		abort();
	}

	loop->count++;

	return NULL;
}

static void
unwatch_fd(netresolve_t channel, int fd, void *handle)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(channel);

	assert(handle == NULL);

	if (epoll_ctl(loop->fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
		error("epoll_ctl: %s", strerror(errno));
		abort();
	}

	loop->count--;
}

static void
free_user_data(void *user_data)
{
	struct netresolve_epoll *loop = user_data;

	assert(!loop->count);

	if (close(loop->fd) == -1)
		abort();
	free(loop);
}

/* netresolve_epoll_install:
 *
 * This function is used internally by netresolve to install the default epoll
 * based main loop to support the blocking mode. In applications, use
 * `netresolve_epoll_open()` instead.
 */
bool
netresolve_epoll_install(netresolve_t channel,
		struct netresolve_epoll *loop,
		netresolve_free_user_data_callback_t free_loop)
{
	if ((loop->fd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
		error("epoll_create1: %s", strerror(errno));
		free(loop);
		return false;
	}

	netresolve_set_fd_callbacks(channel, watch_fd, unwatch_fd);
	netresolve_set_user_data(channel, loop, free_loop);

	return true;
}

/* netresolve_epoll_open:
 *
 * Use this constructor instead of `netresolve_open()` to use the library in
 * a file descriptor based nonblocking mode. Always use
 * `netresolve_epoll_close()` to dispose of the channel.
 */
netresolve_t
netresolve_epoll_open(void)
{
	netresolve_t channel;
	struct netresolve_epoll *loop;

	if (!(channel = netresolve_open()))
		goto fail_channel;

	if (!(loop = calloc(1, sizeof *loop)))
		goto fail_loop;

	if (!netresolve_epoll_install(channel, loop, free_user_data))
		goto fail_install;

	return channel;
fail_install:
	free(loop);
fail_loop:
	netresolve_close(channel);
fail_channel:
	return NULL;
}

/* netresolve_epoll_fd:
 *
 * Retrieve the epoll file descriptor from the channel. Do not add any file
 * descriptors to the epoll instance. Just use the file descriptor in your
 * event loop and poll it for reading.
 */
int
netresolve_epoll_fd(netresolve_t channel)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(channel);

	return loop->fd;
}

static void
dispatch_events(netresolve_t channel, int timeout)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(channel);
	static const int maxevents = 10;
	struct epoll_event events[maxevents];
	int nevents;
	int i;

	nevents = epoll_wait(loop->fd, events, maxevents, timeout);
	switch (nevents) {
	case -1:
		perror("epoll_wait");
		abort();
	case 0:
		return;
	default:
		for (i = 0; i < nevents; i++)
			if (!netresolve_dispatch(channel, events[i].data.ptr, events[i].events))
				abort();
	}
}

/* netresolve_epoll_dispatch:
 *
 * Call this function when the file descriptor is ready for reading.
 */
void
netresolve_epoll_dispatch(netresolve_t channel)
{
	dispatch_events(channel, 0);
}

/* netresolve_epoll_wait:
 *
 * This function is used internally by netresolve to run an epoll based
 * main loop for the channel until all pending queries are fully
 * processed. You wouldn't run it in your application but it is definitely
 * useful for simple testing, as it simulates a real application main
 * loop and allows for processing queries simultaneously.
 */
void
netresolve_epoll_wait(netresolve_t channel)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(channel);

	while (loop->count > 0)
		dispatch_events(channel, -1);
}
