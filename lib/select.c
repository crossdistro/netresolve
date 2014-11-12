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
#include <stdlib.h>
#include <assert.h>

struct netresolve_select {
	fd_set rfds, wfds;
	int nfds;
	void **data;
};

static void *
watch_fd(netresolve_t channel, int fd, int events, void *data)
{
	struct netresolve_select *loop = netresolve_get_user_data(channel);

	assert(fd >= 0);
	assert(!FD_ISSET(fd, &loop->rfds) && !FD_ISSET(fd, &loop->wfds));
	assert(events & (POLLIN | POLLOUT));

	if (events & POLLIN)
		FD_SET(fd, &loop->rfds);
	if (events & POLLOUT)
		FD_SET(fd, &loop->wfds);

	if (fd >= loop->nfds)
		loop->nfds = fd + 1;

	loop->data = realloc(loop->data, loop->nfds * sizeof *loop->data);
	assert(loop->data);

	loop->data[fd] = data;

	return NULL;
}

static void
unwatch_fd(netresolve_t channel, int fd, void *handle)
{
	struct netresolve_select *loop = netresolve_get_user_data(channel);

	assert(fd >= 0);
	assert(FD_ISSET(fd, &loop->rfds) || FD_ISSET(fd, &loop->wfds));
	assert(handle == NULL);
	assert(loop->nfds >= 0);

	FD_CLR(fd, &loop->rfds);
	FD_CLR(fd, &loop->wfds);

	while (loop->nfds > 0 && !FD_ISSET(loop->nfds - 1, &loop->rfds) && !FD_ISSET(loop->nfds - 1, &loop->wfds))
		loop->nfds--;

	loop->data = realloc(loop->data, loop->nfds * sizeof *loop->data);
}

static void
free_user_data(void *user_data)
{
	struct netresolve_select *loop = user_data;

	assert(!loop->nfds);

	free(loop);
}

netresolve_t
netresolve_select_open()
{
	netresolve_t channel;
	struct netresolve_select *loop;

	if (!(loop = calloc(1, sizeof *loop)))
		goto fail;
	if (!(channel = netresolve_open()))
		goto fail_channel;

	netresolve_set_fd_callbacks(channel, watch_fd, unwatch_fd);
	netresolve_set_user_data(channel, loop, free_user_data);

	return channel;
fail_channel:
	free(loop);
fail:
	return NULL;
}

int
netresolve_select_apply_fds(netresolve_t channel, fd_set *rfds, fd_set *wfds)
{
	struct netresolve_select *loop = netresolve_get_user_data(channel);

	for (int fd = 0; fd < loop->nfds; fd++) {
		if (FD_ISSET(fd, &loop->rfds))
			FD_SET(fd, rfds);
		if (FD_ISSET(fd, &loop->wfds))
			FD_SET(fd, wfds);
	}

	return loop->nfds;
}

void
netresolve_select_dispatch_read(netresolve_t channel, int fd)
{
	struct netresolve_select *loop = netresolve_get_user_data(channel);
	assert(fd >= 0);
	assert(fd < loop->nfds);
	assert(FD_ISSET(fd, &loop->rfds));

	netresolve_dispatch(channel, loop->data[fd], POLLIN);
}

void
netresolve_select_dispatch_write(netresolve_t channel, int fd)
{
	struct netresolve_select *loop = netresolve_get_user_data(channel);

	assert(fd >= 0);
	assert(fd < loop->nfds);
	assert(FD_ISSET(fd, &loop->wfds));

	netresolve_dispatch(channel, loop->data[fd], POLLOUT);
}

int
netresolve_select_wait(netresolve_t channel, struct timeval *timeout)
{
	fd_set rfds, wfds;
	int nfds, status;
	
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	nfds = netresolve_select_apply_fds(channel, &rfds, &wfds);

	status = select(nfds, &rfds, &wfds, NULL, timeout);

	for (int fd = 0; fd < nfds; fd++) {
		if (FD_ISSET(fd, &rfds))
			netresolve_select_dispatch_read(channel, fd);
		if (FD_ISSET(fd, &wfds))
			netresolve_select_dispatch_write(channel, fd);
	}

	return status;
}
