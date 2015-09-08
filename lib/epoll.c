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

#define EPOLL_MAXEVENTS 128

static void *
add_watch(netresolve_t context, int fd, int events, netresolve_watch_t watch)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(context);
	struct epoll_event event = { .events = events, .data = { .ptr = watch } };

	assert(watch);

	if (epoll_ctl(loop->fd, EPOLL_CTL_ADD, fd, &event) == -1) {
		error("epoll_ctl: %s", strerror(errno));
		abort();
	}

	loop->count++;

	return watch;
}

static void
remove_watch(netresolve_t context, int fd, void *handle)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(context);
	int i;

	if (epoll_ctl(loop->fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
		error("epoll_ctl: %s", strerror(errno));
		abort();
	}

	/* Disable any pending events. */
	for (i = 0; i < loop->nevents; i++) {
		if (handle == loop->events[i].data.ptr) {
			loop->events[i].data.ptr = NULL;
			debug_context(context, "epoll: disabled pending event %d for removed watch %p", i, handle);
		}
	}

	loop->count--;
}

static void
cleanup(void *user_data)
{
	struct netresolve_epoll *loop = user_data;

	assert(!loop->count);

	if (close(loop->fd) == -1)
		error("close: %s", strerror(errno));

	free(loop->events);
	free(loop);
}

/* netresolve_epoll_install:
 *
 * This function is used internally by netresolve to install the default epoll
 * based main loop to support the blocking mode. In applications, use
 * `netresolve_epoll_new()` instead.
 */
bool
netresolve_epoll_install(netresolve_t context,
		struct netresolve_epoll *loop,
		netresolve_cleanup_callback_t cleanup)
{
	assert(!loop->events);

	if (!(loop->events = calloc(EPOLL_MAXEVENTS, sizeof *loop->events)))
		goto fail_events;

	if ((loop->fd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
		error("epoll_create1: %s", strerror(errno));
		goto fail_epoll;
	}

	netresolve_set_fd_callbacks(context, add_watch, remove_watch, cleanup, loop);

	debug("created epoll file descriptor: %d", loop->fd);

	return true;
fail_epoll:
	free(loop->events);
fail_events:
	free(loop);

	return false;
}

/* netresolve_epoll_new:
 *
 * Use this constructor instead of `netresolve_context_new()` to use the library in
 * a file descriptor based nonblocking mode. Always use
 * `netresolve_epoll_close()` to dispose of the context.
 */
netresolve_t
netresolve_epoll_new(void)
{
	netresolve_t context = netresolve_context_new();

	if (context) {
		if (netresolve_epoll_fd(context) == -1) {
			netresolve_context_free(context);
			context = NULL;
		}
	}

	return context;
}

/* netresolve_epoll_fd:
 *
 * Retrieve the epoll file descriptor from the context. Do not add any file
 * descriptors to the epoll instance. Just use the file descriptor in your
 * event loop and poll it for reading.
 *
 * As a bonus, You can call this function between `netresolve_context_new()` and the
 * first query to convert a blocking context to an epoll based non-blocking
 * one. A sequence of `netresolve_context_new()` and `netresolve_epoll_fd()` is thus
 * equivalent to `netresolve_epoll_new()` with an optional
 * `netresolve_epoll_fd()` to retrieve the file descriptor.
 */
int
netresolve_epoll_fd(netresolve_t context)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(context);

	/* Automatically turn netresolve context into an epoll based nonblocking one */
	if (!loop) {
		if (!(loop = calloc(1, sizeof *loop)))
			return -1;
		if (!netresolve_epoll_install(context, loop, cleanup)) {
			free(loop);
			return -1;
		}
	}

	return loop->fd;
}

static int
dispatch_events(netresolve_t context, int timeout)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(context);
	int i;

	debug_context(context, "epoll: waiting");
	loop->nevents = epoll_wait(loop->fd, loop->events, EPOLL_MAXEVENTS, timeout);

	switch (loop->nevents) {
	case -1:
		error("epoll_wait: %s", strerror(errno));
		abort();
	case 0:
		error("epoll_wait: no events");
		break;
	default:
		debug_context(context, "epoll_wait: new events: %d", loop->nevents);
		for (i = 0; i < loop->nevents; i++)
			if (loop->events[i].data.ptr)
				netresolve_dispatch(context, loop->events[i].data.ptr, loop->events[i].events);
	}

	return loop->nevents;
}

/* netresolve_epoll_dispatch:
 *
 * Call this function when the file descriptor is ready for reading.
 */
void
netresolve_epoll_dispatch(netresolve_t context)
{
	/* FIXME: We currently need to run the dispatch events repeatedly as long
	 * as there are still pending events in order to keep asyncns_wait working
	 * correctly with block=0 as it relies on all pending events being
	 * processed.
	 */
	while (dispatch_events(context, 0) > 0)
		;
}

/* netresolve_epoll_wait:
 *
 * This function is used internally by netresolve to run an epoll based
 * main loop for the context until all pending queries are fully
 * processed. You wouldn't run it in your application but it is definitely
 * useful for simple testing, as it simulates a real application main
 * loop and allows for processing queries simultaneously.
 */
void
netresolve_epoll_wait(netresolve_t context)
{
	struct netresolve_epoll *loop = netresolve_get_user_data(context);

	while (loop->count > 0)
		dispatch_events(context, -1);
}
