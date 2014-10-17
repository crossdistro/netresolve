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
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <errno.h>

#include "netresolve-private.h"

static bool
strtob(const char *string)
{
	return string && (!strcasecmp(string, "yes") || !strcasecmp(string, "true") || !strcasecmp(string, "1"));
}

netresolve_t
netresolve_open(void)
{
	/* FIXME: this should probably be only called once */
	netresolve_set_log_level(strtob(secure_getenv("NETRESOLVE_VERBOSE")) ? NETRESOLVE_LOG_LEVEL_DEBUG : NETRESOLVE_LOG_LEVEL_QUIET);

	netresolve_t channel = calloc(1, sizeof *channel);
	if (!channel)
		return NULL;

	channel->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (channel->epoll_fd == -1) {
		free(channel);
		return NULL;
	}

	channel->request.default_loopback = strtob(secure_getenv("NETRESOLVE_FLAG_DEFAULT_LOOPBACK"));
	channel->config.force_family = netresolve_family_from_string(secure_getenv("NETRESOLVE_FORCE_FAMILY"));

	return channel;
}

void
netresolve_close(netresolve_t channel)
{
	int i;

	if (!channel)
		return;

	for (i = 0; i < channel->nqueries; i++)
		netresolve_query_done(channel->queries[i]);
	free(channel->queries);

	netresolve_set_backend_string(channel, "");
	close(channel->epoll_fd);
	memset(channel, 0, sizeof *channel);
	free(channel);
}

static const char *
state_to_string(enum netresolve_state state)
{
	switch (state) {
	case NETRESOLVE_STATE_INIT:
		return "init";
	case NETRESOLVE_STATE_WAITING:
		return "waiting";
	case NETRESOLVE_STATE_FINISHED:
		return "finished";
	case NETRESOLVE_STATE_FAILED:
		return "failed";
	default:
		/* Shouldn't happen. */
		return "UNKNOWN";
	}
}

void
netresolve_query_set_state(netresolve_query_t query, enum netresolve_state state)
{
	enum netresolve_state old_state = query->state;

	query->state = state;

	debug("state: %s -> %s\n", state_to_string(old_state), state_to_string(state));

	/* Leaving state... */
	switch (old_state) {
	case NETRESOLVE_STATE_WAITING:
		if (query->channel->callbacks.watch_fd)
			query->channel->callbacks.watch_fd(query, query->channel->epoll_fd, 0,
					query->channel->callbacks.user_data_fd);
		break;
	default:
		break;
	}

	/* Entering state... */
	switch (state) {
	case NETRESOLVE_STATE_INIT:
		free(query->request.dns_name);
		free(query->response.paths);
		free(query->response.canonname);
		memset(&query->response, 0, sizeof query->response);
		break;
	case NETRESOLVE_STATE_WAITING:
		if (query->channel->callbacks.watch_fd)
			query->channel->callbacks.watch_fd(query, query->channel->epoll_fd, POLLIN,
					query->channel->callbacks.user_data_fd);
		netresolve_query_start(query);
		break;
	case NETRESOLVE_STATE_FINISHED:
		if (!query->response.canonname)
			query->response.canonname = query->request.nodename ? strdup(query->request.nodename) : strdup("localhost");
		if (query->channel->callbacks.on_connect)
			netresolve_connect_cleanup(query);
		if (query->channel->callbacks.on_success)
			query->channel->callbacks.on_success(query, query->channel->callbacks.user_data);
		break;
	case NETRESOLVE_STATE_FAILED:
		if (query->channel->callbacks.on_success)
			query->channel->callbacks.on_success(query, query->channel->callbacks.user_data);
		break;
	}
}

void
netresolve_query_start(netresolve_query_t query)
{
	struct netresolve_backend *backend = *query->backend;
	void (*setup)(netresolve_query_t query, char **settings);

	debug("starting backend: %s\n", backend->settings[0]);
	setup = backend->setup[query->request.type];
	if (setup)
		setup(query, backend->settings + 1);
	else
		netresolve_backend_failed(query);
}

static bool
dispatch_fd(netresolve_t channel, int fd, int events)
{
	int i;

	for (i = 0; i < channel->nqueries; i++) {
		netresolve_query_t query = channel->queries[i];
		struct netresolve_backend *backend = *query->backend;

		if (query->state != NETRESOLVE_STATE_WAITING)
			continue;

		if (!backend && netresolve_connect_dispatch(query, fd, events))
			return true;

		if (backend && backend->dispatch) {
			backend->dispatch(query, fd, events);
			return true;
		}
	}

	return false;
}

bool
netresolve_epoll(netresolve_t channel, int timeout)
{
	static const int maxevents = 1;
	struct epoll_event events[maxevents];
	int nevents;
	int i;

	assert(channel->epoll_count > 0);

	nevents = epoll_wait(channel->epoll_fd, events, maxevents, channel->callbacks.watch_fd ? 0 : -1);
	if (nevents == -1)
		return false;
	for (i = 0; i < nevents; i++)
		dispatch_fd(channel, events[i].data.fd, events[i].events);

	return true;
}

void
netresolve_watch_fd(netresolve_t channel, int fd, int events)
{
	struct epoll_event event = { .events = events, .data = { .fd = fd} };

	debug("watching file descriptor: %d %d\n", fd, events);

	if (epoll_ctl(channel->epoll_fd, EPOLL_CTL_DEL, fd, &event) != -1)
		channel->epoll_count--;
	else if (errno != ENOENT && errno != EBADF)
		error("epoll_ctl: %s", strerror(errno));

	if (!events)
		return;

	if (epoll_ctl(channel->epoll_fd, EPOLL_CTL_ADD, fd, &event) != -1)
		channel->epoll_count++;
	else
		error("epoll_ctl: %s", strerror(errno));
}

int
netresolve_add_timeout(netresolve_t channel, time_t sec, long nsec)
{
	int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	struct itimerspec timerspec = {{0, 0}, {sec, nsec}};

	if (fd != -1) {
		if (timerfd_settime(fd, 0, &timerspec, NULL) == -1) {
			close(fd);
			return -1;
		}
		netresolve_watch_fd(channel, fd, POLLIN);
	}

	debug("added timeout: %d %d %d\n", fd, (int) sec, (int) nsec);

	return fd;
}

void
netresolve_remove_timeout(netresolve_t channel, int fd)
{
	netresolve_watch_fd(channel, fd, 0);
	debug("removed timeout: %d\n", fd);
	close(fd);
}

static int
state_to_errno(enum netresolve_state state)
{
	switch (state) {
	case NETRESOLVE_STATE_WAITING:
		return EWOULDBLOCK;
	case NETRESOLVE_STATE_FINISHED:
		return 0;
	case NETRESOLVE_STATE_FAILED:
		return ENODATA;
	default:
		/* Shouldn't happen. */
		return -1;
	}
}

netresolve_query_t
netresolve_query_new(netresolve_t channel, enum netresolve_request_type type)
{
	netresolve_query_t query;
	netresolve_query_t *queries;

	if (!(query = calloc(1, sizeof *query)))
		return NULL;
	if (!(queries = realloc(channel->queries, ++channel->nqueries * sizeof *queries))) {
		free(query);
		return NULL;
	}

	query->channel = channel;
	channel->queries = queries;
	channel->queries[channel->nqueries - 1] = query;

	if (!channel->backends)
		netresolve_set_backend_string(channel, secure_getenv("NETRESOLVE_BACKENDS"));
	if (!channel->backends || !*channel->backends) {
		netresolve_query_set_state(query, NETRESOLVE_STATE_FINISHED);
		return query;
	}

	query->first_connect_timeout = -1;
	query->backend = channel->backends;
	memcpy(&query->request, &channel->request, sizeof channel->request);

	query->request.type = type;

	return query;
}

static netresolve_query_t
netresolve_query_run(netresolve_query_t query)
{
	netresolve_t channel = query->channel;

	netresolve_query_set_state(query, NETRESOLVE_STATE_WAITING);

	/* Blocking mode. */
	if (!channel->callbacks.watch_fd)
		while (query->state == NETRESOLVE_STATE_WAITING)
			netresolve_epoll(channel, -1);

	errno = state_to_errno(query->state);
	return query;
}

netresolve_query_t
netresolve_query(netresolve_t channel, const char *nodename, const char *servname)
{
	netresolve_query_t query = netresolve_query_new(channel, NETRESOLVE_REQUEST_FORWARD);

	if (!query)
		return NULL;

	if (channel->config.force_family)
		query->request.family = channel->config.force_family;
	query->request.nodename = nodename;
	query->request.servname = servname;

	return netresolve_query_run(query);
}

netresolve_query_t
netresolve_query_reverse(netresolve_t channel, int family, const void *address, int ifindex, int port)
{
	netresolve_query_t query = netresolve_query_new(channel, NETRESOLVE_REQUEST_REVERSE);
	size_t size = family == AF_INET ? 4 : 16;

	if (!query)
		return NULL;

	query->request.family = family;
	memcpy(query->request.address, address, size);

	return netresolve_query_run(query);
}

netresolve_query_t
netresolve_query_dns(netresolve_t channel, const char *dname, int cls, int type)
{
	netresolve_query_t query = netresolve_query_new(channel, NETRESOLVE_REQUEST_DNS);

	if (!query)
		return NULL;

	query->request.dns_name = strdup(dname);
	query->request.dns_class = cls;
	query->request.dns_type = type;

	return netresolve_query_run(query);
}

bool
netresolve_dispatch_fd(netresolve_t channel, int fd, int events)
{
	if (fd != channel->epoll_fd) {
		errno = EBADF;
		return false;
	}
	if (events != EPOLLIN) {
		errno = EINVAL;
		return false;
	}

	return netresolve_epoll(channel, 0);
}

/* netresolve_query_done:
 *
 * Call this function when you are finished with the netresolve query and
 * don't need to access it any more. It cancels the query if it hasn't been
 * finished yet, and performs internal cleanups. Don't use the query handle
 * after calling it.
 */
void
netresolve_query_done(netresolve_query_t query)
{
	netresolve_t channel = query->channel;
	int i;

	netresolve_query_set_state(query, NETRESOLVE_STATE_INIT);

	for (i = 0; i < channel->nqueries; i++)
		if (channel->queries[i] == query)
			break;
	assert(i < channel->nqueries);

	memmove(&channel->queries[i], &channel->queries[i+1], --channel->nqueries - i);
	channel->queries = realloc(channel->queries, channel->nqueries * sizeof *channel->queries);

	free(query);
}
