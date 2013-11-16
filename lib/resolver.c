/* This file is part of the `netresolve` library.
 * Copyright (C) Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <errno.h>

#include "netresolve-private.h"

static bool
strtob(const char *string)
{
	return string && (!strcasecmp(string, "yes") || !strcasecmp(string, "true"));
}

netresolve_t
netresolve_open(void)
{
	netresolve_t resolver = calloc(1, sizeof *resolver);

	if (!resolver)
		return NULL;

	resolver->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (resolver->epoll_fd == -1) {
		free(resolver);
		return NULL;
	}

	if (strtob(getenv("NETRESOLVE_FLAG_DEFAULT_LOOPBACK")))
		netresolve_set_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);

	return resolver;
}

static void
free_backend(struct netresolve_backend *backend)
{
	char **p;

	if (!backend)
		return;
	if (backend->settings) {
		for (p = backend->settings; *p; p++)
			free(*p);
		free(backend->settings);
	}
	if (backend->dl_handle)
		dlclose(backend->dl_handle);
	free(backend);
}

static void
clear_backends(netresolve_t resolver)
{
	struct netresolve_backend **backend;

	if (!resolver->backends)
		return;

	for (backend = resolver->backends; *backend; backend++)
		free_backend(*backend);
	free(resolver->backends);
	resolver->backends = NULL;
}

void
netresolve_close(netresolve_t resolver)
{
	if (!resolver)
		return;
	_netresolve_set_state(resolver, NETRESOLVE_STATE_INIT);
	close(resolver->epoll_fd);
	free(resolver->backend_string);
	clear_backends(resolver);
	memset(resolver, 0, sizeof *resolver);
	free(resolver);
}

void
netresolve_set_log_level(netresolve_t resolver, int level)
{
	resolver->log_level = level;
}

void
netresolve_set_flag(netresolve_t resolver, netresolve_flag_t flag)
{
	if (flag >= _NETRESOLVE_FLAG_COUNT)
		return;

	resolver->request.flags |= (1 << flag);
}

void
netresolve_unset_flag(netresolve_t resolver, netresolve_flag_t flag)
{
	if (flag >= _NETRESOLVE_FLAG_COUNT)
		return;

	resolver->request.flags &= ~(1 << flag);
}

void
netresolve_callback_set_callbacks(netresolve_t resolver,
		netresolve_callback_t on_success,
		netresolve_callback_t on_failure,
		void *user_data)
{
	resolver->callbacks.on_success = on_success;
	resolver->callbacks.on_failure = on_failure;
	resolver->callbacks.user_data = user_data;
}

void
netresolve_callback_set_watch_fd(netresolve_t resolver,
		netresolve_fd_callback_t watch_fd,
		void *user_data)
{
	resolver->callbacks.watch_fd = watch_fd;
	resolver->callbacks.user_data_fd = user_data;
}

void
netresolve_callback_set_bind(netresolve_t resolver,
		netresolve_socket_callback_t on_bind,
		void *user_data)
{
	resolver->callbacks.on_bind = on_bind;
	resolver->callbacks.on_connect = NULL;
	resolver->callbacks.user_data_sock = user_data;

	netresolve_unset_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);
}

void
netresolve_callback_set_connect(netresolve_t resolver,
		netresolve_socket_callback_t on_connect,
		void *user_data)
{
	resolver->callbacks.on_bind = NULL;
	resolver->callbacks.on_connect = on_connect;
	resolver->callbacks.user_data_sock = user_data;

	netresolve_set_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);
}

static struct netresolve_backend *
load_backend(char **take_settings)
{
	struct netresolve_backend *backend = calloc(1, sizeof *backend);
	const char *name;
	char filename[1024];

	if (!backend)
		return NULL;
	if (!take_settings || !*take_settings)
		goto fail;

	name = *take_settings;
	if (*name == '+') {
		backend->mandatory = true;
		name++;
	}

	backend->settings = take_settings;
	snprintf(filename, sizeof filename, "libnetresolve-backend-%s.so", name);
	backend->dl_handle = dlopen(filename, RTLD_NOW);
	if (!backend->dl_handle) {
		error("%s\n", dlerror());
		goto fail;
	}

	backend->start = dlsym(backend->dl_handle, "start");
	backend->dispatch = dlsym(backend->dl_handle, "dispatch");
	backend->cleanup = dlsym(backend->dl_handle, "cleanup");

	if (!backend->start)
		goto fail;

	return backend;
fail:
	free_backend(backend);
	return NULL;
}

void
netresolve_set_backend_string(netresolve_t resolver, const char *string)
{
	const char *start, *end;
	char **settings = NULL;
	int nsettings = 0;
	int nbackends = 0;

	/* Default. */
	if (string == NULL)
		string = "unix,any,loopback,numerichost,hosts,dns";

	/* Install new set of backends. */
	clear_backends(resolver);
	for (start = end = string; true; end++) {
		if (*end == ':' || *end == ',' || *end == '\0') {
			settings = realloc(settings, (nsettings + 2) * sizeof *settings);
			settings[nsettings++] = strndup(start, end - start);
			settings[nsettings] = NULL;
			start = end + 1;
		}
		if (*end == ',' || *end == '\0') {
			resolver->backends = realloc(resolver->backends, (nbackends + 2) * sizeof *resolver->backends);
			resolver->backends[nbackends] = load_backend(settings);
			if (resolver->backends[nbackends]) {
				nbackends++;
				resolver->backends[nbackends] = NULL;
			}
			nsettings = 0;
			settings = NULL;
		}
		if (*end == '\0') {
			break;
		}
	}
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
_netresolve_set_state(netresolve_t resolver, enum netresolve_state state)
{
	enum netresolve_state old_state = resolver->state;

	resolver->state = state;

	debug("state: %s -> %s\n", state_to_string(old_state), state_to_string(state));

	/* Leaving state... */
	switch (old_state) {
	case NETRESOLVE_STATE_WAITING:
		if (resolver->callbacks.watch_fd)
			resolver->callbacks.watch_fd(resolver, resolver->epoll_fd, 0,
					resolver->callbacks.user_data_fd);
		break;
	default:
		break;
	}

	/* Entering state... */
	switch (state) {
	case NETRESOLVE_STATE_INIT:
		free(resolver->response.paths);
		free(resolver->response.canonname);
		memset(&resolver->response, 0, sizeof resolver->response);
		break;
	case NETRESOLVE_STATE_WAITING:
		if (resolver->callbacks.watch_fd)
			resolver->callbacks.watch_fd(resolver, resolver->epoll_fd, POLLIN,
					resolver->callbacks.user_data_fd);
		_netresolve_start(resolver);
		break;
	case NETRESOLVE_STATE_FINISHED:
		if (resolver->callbacks.on_connect)
			_netresolve_connect(resolver);
		if (resolver->callbacks.on_success)
			resolver->callbacks.on_success(resolver, resolver->callbacks.user_data);
		break;
	case NETRESOLVE_STATE_FAILED:
		if (resolver->callbacks.on_success)
			resolver->callbacks.on_success(resolver, resolver->callbacks.user_data);
		break;
	}
}

void
_netresolve_start(netresolve_t resolver)
{
	struct netresolve_backend *backend = *resolver->backend;

	backend->start(resolver, backend->settings+1);
}

bool
_netresolve_dispatch_fd(netresolve_t resolver, int fd, int events)
{
	struct netresolve_backend *backend = *resolver->backend;

	if (backend && backend->dispatch) {
		backend->dispatch(resolver, fd, events);
		return true;
	}

	return false;
}

void
_netresolve_epoll(netresolve_t resolver, int timeout)
{
	static const int maxevents = 1;
	struct epoll_event events[maxevents];
	int nevents;
	int i;

	/* Sanity check number of descriptors. */
	if (resolver->epoll_count <= 0) {
		_netresolve_set_state(resolver, NETRESOLVE_STATE_FAILED);
		return;
	}

	nevents = epoll_wait(resolver->epoll_fd, events, maxevents, resolver->callbacks.watch_fd ? 0 : -1);
	if (nevents == -1) {
		_netresolve_set_state(resolver, NETRESOLVE_STATE_FAILED);
		return;
	}
	for (i = 0; resolver->state == NETRESOLVE_STATE_WAITING && i < nevents; i++)
		_netresolve_dispatch_fd(resolver, events[i].data.fd, events[i].events);
}

void
_netresolve_watch_fd(netresolve_t resolver, int fd, int events)
{
	struct epoll_event event = { .events = events, .data = { .fd = fd} };

	debug("watching file descriptor: %d %d\n", fd, events);

	if (!resolver->backend || resolver->epoll_fd == -1)
		abort();

	if (epoll_ctl(resolver->epoll_fd, EPOLL_CTL_DEL, fd, &event) != -1)
		resolver->epoll_count--;
	else if (errno != ENOENT && errno != EBADF)
		error("epoll_ctl: %s", strerror(errno));

	if (!events)
		return;

	if (epoll_ctl(resolver->epoll_fd, EPOLL_CTL_ADD, fd, &event) != -1)
		resolver->epoll_count++;
	else
		error("epoll_ctl: %s", strerror(errno));
}

int
_netresolve_add_timeout(netresolve_t resolver, time_t sec, long nsec)
{
	int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	struct itimerspec timerspec = {{0, 0}, {sec, nsec}};

	if (fd != -1) {
		if (timerfd_settime(fd, 0, &timerspec, NULL) == -1) {
			close(fd);
			return -1;
		}
		_netresolve_watch_fd(resolver, fd, POLLIN);
	}

	debug("added timeout: %d %d %d\n", fd, (int) sec, (int) nsec);

	return fd;
}

void
_netresolve_remove_timeout(netresolve_t resolver, int fd)
{
	_netresolve_watch_fd(resolver, fd, 0);
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

int
netresolve_resolve(netresolve_t resolver,
		const char *node, const char *service, int family, int socktype, int protocol)
{
	if (resolver->state == NETRESOLVE_STATE_WAITING)
		return EBUSY;
	_netresolve_set_state(resolver, NETRESOLVE_STATE_INIT);
	if (!resolver->backends)
		netresolve_set_backend_string(resolver, getenv("NETRESOLVE_BACKENDS"));
	if (!resolver->backends)
		return ENODATA;
	resolver->backend = resolver->backends;

	resolver->request.node = node;
	resolver->request.service = service;
	resolver->request.family = family;
	resolver->request.socktype = socktype;
	resolver->request.protocol = protocol;

	_netresolve_set_state(resolver, NETRESOLVE_STATE_WAITING);

	/* Blocking mode. */
	if (!resolver->callbacks.watch_fd)
		while (resolver->state == NETRESOLVE_STATE_WAITING)
			_netresolve_epoll(resolver, -1);

	return state_to_errno(resolver->state);
}

int
netresolve_dispatch(netresolve_t resolver, int fd, int events)
{
	if (fd != resolver->epoll_fd || events != EPOLLIN)
		return EINVAL;

	_netresolve_epoll(resolver, 0);

	return state_to_errno(resolver->state);
}

void
netresolve_cancel(netresolve_t resolver)
{
	_netresolve_set_state(resolver, NETRESOLVE_STATE_INIT);
}
