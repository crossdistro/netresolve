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
#include <netresolve-private.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

void
netresolve_set_fd_callbacks(netresolve_t context,
		netresolve_watch_fd_callback_t watch_fd,
		netresolve_unwatch_fd_callback_t unwatch_fd,
		void *user_data,
		netresolve_free_user_data_callback_t free_user_data)
{
	assert(watch_fd && unwatch_fd);
	assert(!context->callbacks.watch_fd && !context->callbacks.unwatch_fd);

	context->callbacks.watch_fd = watch_fd;
	context->callbacks.unwatch_fd = unwatch_fd;

	context->callbacks.user_data = user_data;
	context->callbacks.free_user_data = free_user_data;
}

void *
netresolve_get_user_data(netresolve_t context)
{
    return context->callbacks.user_data;
}

void
netresolve_watch_fd(netresolve_query_t query, int fd, int events)
{
	struct netresolve_source *sources = &query->sources;
	struct netresolve_source *source;

	assert(fd >= 0);
	assert(events && !(events & ~(POLLIN | POLLOUT)));

	if (!(source = calloc(1, sizeof(*source))))
		abort();

	source->query = query;
	source->fd = fd;
	source->handle = query->context->callbacks.watch_fd(query->context, fd, events, source);

	source->previous = sources->previous;
	source->next = sources;
	source->previous->next = source->next->previous = source;

	query->nfds++;
	query->context->nfds++;

	debug_query(query, "added file descriptor: fd=%d events=%d (total %d/%d)", fd, events, query->nfds, query->context->nfds);
}

void
netresolve_unwatch_fd(netresolve_query_t query, int fd)
{
	struct netresolve_source *sources = &query->sources;
	struct netresolve_source *source;

	assert(fd >= 0);

	for (source = sources->next; source != sources; source = source->next)
		if (source->fd == fd)
			break;

	assert(query->nfds > 0);
	assert(query->context->nfds > 0);
	assert(source != sources);

	query->context->callbacks.unwatch_fd(query->context, fd, source->handle);

	source->previous->next = source->next;
	source->next->previous = source->previous;
	free(source);

	query->nfds--;
	query->context->nfds--;

	debug_query(query, "removed file descriptor: fd=%d (total %d/%d)", fd, query->nfds, query->context->nfds);
}

int
netresolve_add_timeout(netresolve_query_t query, time_t sec, long nsec)
{
	int fd;
	struct itimerspec timerspec = {{0, 0}, {sec, nsec}};

	if ((fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1)
		return -1;

	if (timerfd_settime(fd, 0, &timerspec, NULL) == -1) {
		close(fd);
		return -1;
	}

	debug_query(query, "adding timeout: fd=%d sec=%d nsec=%ld", fd, (int) sec, nsec);

	netresolve_watch_fd(query, fd, POLLIN);

	return fd;
}

int
netresolve_add_timeout_ms(netresolve_query_t query, time_t msec)
{
	return netresolve_add_timeout(query, msec / 1000, (msec % 1000) * 1000000L);
}

void
netresolve_remove_timeout(netresolve_query_t query, int fd)
{
	netresolve_unwatch_fd(query, fd);
	close(fd);

	debug_query(query, "removed timeout: fd=%d", fd);
}

bool
netresolve_dispatch(netresolve_t context, netresolve_source_t source, int events)
{
	assert(source);

	if (!(events & (POLLIN | POLLOUT)) || (events & ~(POLLIN | POLLOUT))) {
		error("Bad poll events %d for source %p.", events, source);
		return false;
	}

	debug_query(source->query, "dispatching: fd=%d events=%d", source->fd, events);

	return netresolve_query_dispatch(source->query, source->fd, events);
}
