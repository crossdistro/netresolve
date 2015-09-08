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
#include <netresolve-select.h>
#include <netresolve-private.h>
#include <sys/poll.h>
#include <assert.h>

struct netresolve_select {
	fd_set rfds, wfds;
	int nfds;
	netresolve_watch_t *sources;
};

static void *
add_watch(netresolve_t context, int fd, int events, netresolve_watch_t source)
{
	struct netresolve_select *loop = netresolve_get_user_data(context);

	assert(fd >= 0);
	assert(!FD_ISSET(fd, &loop->rfds) && !FD_ISSET(fd, &loop->wfds));
	assert(events & (POLLIN | POLLOUT));

	if (events & POLLIN)
		FD_SET(fd, &loop->rfds);
	if (events & POLLOUT)
		FD_SET(fd, &loop->wfds);

	if (fd >= loop->nfds)
		loop->nfds = fd + 1;

	loop->sources = realloc(loop->sources, loop->nfds * sizeof *loop->sources);
	assert(loop->sources);

	loop->sources[fd] = source;

	return NULL;
}

static void
remove_watch(netresolve_t context, int fd, void *handle)
{
	struct netresolve_select *loop = netresolve_get_user_data(context);

	assert(fd >= 0);
	assert(FD_ISSET(fd, &loop->rfds) || FD_ISSET(fd, &loop->wfds));
	assert(handle == NULL);
	assert(loop->nfds >= 0);

	FD_CLR(fd, &loop->rfds);
	FD_CLR(fd, &loop->wfds);

	while (loop->nfds > 0 && !FD_ISSET(loop->nfds - 1, &loop->rfds) && !FD_ISSET(loop->nfds - 1, &loop->wfds))
		loop->nfds--;

	loop->sources = realloc(loop->sources, loop->nfds * sizeof *loop->sources);
}

static void
cleanup(void *user_data)
{
	struct netresolve_select *loop = user_data;

	assert(!loop->nfds);

	free(loop);
}

netresolve_t
netresolve_select_new()
{
	netresolve_t context;
	struct netresolve_select *loop;

	if (!(loop = calloc(1, sizeof *loop)))
		goto fail;
	if (!(context = netresolve_context_new()))
		goto fail_context;

	netresolve_set_fd_callbacks(context, add_watch, remove_watch, cleanup, loop);

	return context;
fail_context:
	free(loop);
fail:
	return NULL;
}

int
netresolve_select_apply_fds(netresolve_t context, fd_set *rfds, fd_set *wfds)
{
	struct netresolve_select *loop = netresolve_get_user_data(context);

	for (int fd = 0; fd < loop->nfds; fd++) {
		if (FD_ISSET(fd, &loop->rfds))
			FD_SET(fd, rfds);
		if (FD_ISSET(fd, &loop->wfds))
			FD_SET(fd, wfds);
	}

	return loop->nfds;
}

void
netresolve_select_dispatch_read(netresolve_t context, int fd)
{
	struct netresolve_select *loop = netresolve_get_user_data(context);
	assert(fd >= 0);
	assert(fd < loop->nfds);
	assert(FD_ISSET(fd, &loop->rfds));

	netresolve_dispatch(context, loop->sources[fd], POLLIN);
}

void
netresolve_select_dispatch_write(netresolve_t context, int fd)
{
	struct netresolve_select *loop = netresolve_get_user_data(context);

	assert(fd >= 0);
	assert(fd < loop->nfds);
	assert(FD_ISSET(fd, &loop->wfds));

	netresolve_dispatch(context, loop->sources[fd], POLLOUT);
}

int
netresolve_select_wait(netresolve_t context, struct timeval *timeout)
{
	fd_set rfds, wfds;
	int nfds, status;
	
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	nfds = netresolve_select_apply_fds(context, &rfds, &wfds);

	status = select(nfds, &rfds, &wfds, NULL, timeout);

	for (int fd = 0; fd < nfds; fd++) {
		if (FD_ISSET(fd, &rfds))
			netresolve_select_dispatch_read(context, fd);
		if (FD_ISSET(fd, &wfds))
			netresolve_select_dispatch_write(context, fd);
	}

	return status;
}
