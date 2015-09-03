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
		netresolve_watch_add_callback_t add_watch,
		netresolve_watch_remove_callback_t remove_watch,
		void *user_data,
		netresolve_free_user_data_callback_t free_user_data)
{
	assert(add_watch && remove_watch);
	assert(!context->callbacks.add_watch && !context->callbacks.remove_watch);

	context->callbacks.add_watch = add_watch;
	context->callbacks.remove_watch = remove_watch;

	context->callbacks.user_data = user_data;
	context->callbacks.free_user_data = free_user_data;
}

void *
netresolve_get_user_data(netresolve_t context)
{
    return context->callbacks.user_data;
}

netresolve_watch_t
netresolve_watch_add(netresolve_query_t query, int fd, int events,
		netresolve_watch_callback_t callback, void *data)
{
	struct netresolve_watch *watches = &query->watches;
	struct netresolve_watch *watch;

	assert(fd >= 0);
	assert(events && !(events & ~(POLLIN | POLLOUT)));

	if (!(watch = calloc(1, sizeof(*watch))))
		abort();

	watch->query = query;
	watch->fd = fd;
	watch->callback = callback;
	watch->data = data;
	watch->handle = query->context->callbacks.add_watch(query->context, fd, events, watch);

	watch->previous = watches->previous;
	watch->next = watches;
	watch->previous->next = watch->next->previous = watch;

	query->nfds++;
	query->context->nfds++;

	debug_query(query, "added file descriptor: fd=%d events=%d watch=%p (total %d/%d)", fd, events, watch, query->nfds, query->context->nfds);

	assert(watch);

	return watch;
}

void
netresolve_watch_remove(netresolve_query_t query, netresolve_watch_t watch, bool do_close)
{
	struct netresolve_watch *watches = &query->watches;

	assert(query->nfds > 0);
	assert(query->context->nfds > 0);
	assert(watch != watches);

	watch->previous->next = watch->next;
	watch->next->previous = watch->previous;

	query->nfds--;
	query->context->nfds--;

	query->context->callbacks.remove_watch(query->context, watch->fd, watch->handle);

	debug_query(query, "removed file descriptor: fd=%d watch=%p (total %d/%d)", watch->fd, watch, query->nfds, query->context->nfds);

	if (do_close) {
		close(watch->fd);
		debug_query(query, "closed file descriptor: fd=%d", watch->fd);
	}

	memset(watch, 0, sizeof *watch);
	free(watch);
}

static void
timeout_watch_callback(netresolve_query_t query, netresolve_watch_t watch, int fd, int events, void *data)
{
	watch->timeout_callback(query, watch, data);
}

netresolve_timeout_t
netresolve_timeout_add(netresolve_query_t query, time_t sec, long nsec,
		netresolve_timeout_callback_t callback, void *data)
{
	netresolve_watch_t watch;
	int fd;
	struct itimerspec timerspec = {{0, 0}, {sec, nsec}};

	if ((fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1)
		return NULL;

	if (timerfd_settime(fd, 0, &timerspec, NULL) == -1) {
		close(fd);
		return NULL;
	}

	debug_query(query, "adding timeout: fd=%d sec=%d nsec=%ld", fd, (int) sec, nsec);

	watch = netresolve_watch_add(query, fd, POLLIN, NULL, data);

	if (watch && callback) {
		watch->callback = timeout_watch_callback;
		watch->timeout_callback = callback;
	}

	assert(watch);

	return watch;
}

netresolve_timeout_t
netresolve_timeout_add_ms(netresolve_query_t query, time_t msec,
		netresolve_timeout_callback_t callback, void *data)
{
	return netresolve_timeout_add(query, msec / 1000, (msec % 1000) * 1000000L, callback, data);
}

void
netresolve_timeout_remove(netresolve_query_t query, netresolve_timeout_t timeout)
{
	debug_query(query, "removing timeout: %p timeoutfd=%d", timeout, timeout->fd);

	netresolve_watch_remove(query, timeout, true);
}

bool
netresolve_dispatch(netresolve_t context, netresolve_watch_t watch, int events)
{
	assert(watch);
	assert(watch->query);

	if (!(events & (POLLIN | POLLOUT)) || (events & ~(POLLIN | POLLOUT))) {
		error("Bad poll events %d for watch %p.", events, watch);
		return false;
	}

	debug_query(watch->query, "dispatching: fd=%d events=%d watch=%p", watch->fd, events, watch);

	return netresolve_query_dispatch(watch->query, watch, watch->fd, events, watch->data);
}
