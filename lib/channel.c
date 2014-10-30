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
#include <errno.h>
#include <dlfcn.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

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

static bool
dispatch_fd(netresolve_t channel, int fd, int events)
{
	int i;

	for (i = 0; i < channel->nqueries; i++) {
		netresolve_query_t query = channel->queries[i];
		struct netresolve_backend *backend = *query->backend;

		if (query->state != NETRESOLVE_STATE_WAITING)
			continue;

		if (query->delayed_state && fd == query->delayed_fd) {
			enum netresolve_state state = query->delayed_state;

			query->delayed_state = NETRESOLVE_STATE_NONE;
			close(query->delayed_fd);
			query->delayed_fd = -1;

			netresolve_query_set_state(query, state);
			return true;
		}

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
	static const int maxevents = 10;
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

	debug("watching file descriptor: %d %d", fd, events);

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

	debug("added timeout: %d %d %d", fd, (int) sec, (int) nsec);

	return fd;
}

void
netresolve_remove_timeout(netresolve_t channel, int fd)
{
	netresolve_watch_fd(channel, fd, 0);
	debug("removed timeout: %d", fd);
	close(fd);
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

	netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);

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
	query->request.port = port;

	netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);

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

	netresolve_query_set_state(query, NETRESOLVE_STATE_SETUP);

	return netresolve_query_run(query);
}

bool
netresolve_dispatch_fd(netresolve_t channel, int fd, int events)
{
	if (fd != channel->epoll_fd && fd != -1) {
		errno = EBADF;
		return false;
	}
	if (events != EPOLLIN) {
		errno = EINVAL;
		return false;
	}

	return netresolve_epoll(channel, 0);
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
		error("%s", dlerror());
		goto fail;
	}

	backend->setup[NETRESOLVE_REQUEST_FORWARD] = dlsym(backend->dl_handle, "setup_forward");
	backend->setup[NETRESOLVE_REQUEST_REVERSE] = dlsym(backend->dl_handle, "setup_reverse");
	backend->setup[NETRESOLVE_REQUEST_DNS] = dlsym(backend->dl_handle, "setup_dns");
	backend->dispatch = dlsym(backend->dl_handle, "dispatch");
	backend->cleanup = dlsym(backend->dl_handle, "cleanup");

	if (!backend->setup)
		goto fail;

	return backend;
fail:
	free_backend(backend);
	return NULL;
}

void
netresolve_set_backend_string(netresolve_t channel, const char *string)
{
	const char *setup, *end;
	char **settings = NULL;
	int nsettings = 0;
	int nbackends = 0;

	/* Default */
	if (string == NULL)
		string = "unix,any,loopback,numerichost,hosts,hostname,ubdns";

	/* Clear old backends */
	if (channel->backends) {
		struct netresolve_backend **backend;

		for (backend = channel->backends; *backend; backend++)
			free_backend(*backend);
		free(channel->backends);
		channel->backends = NULL;
	}

	/* Install new set of backends */
	for (setup = end = string; true; end++) {
		if (*end == ':' || *end == ',' || *end == '\0') {
			settings = realloc(settings, (nsettings + 2) * sizeof *settings);
			settings[nsettings++] = strndup(setup, end - setup);
			settings[nsettings] = NULL;
			setup = end + 1;
		}
		if (*end == ',' || *end == '\0') {
			if (settings && *settings && **settings) {
				channel->backends = realloc(channel->backends, (nbackends + 2) * sizeof *channel->backends);
				channel->backends[nbackends] = load_backend(settings);
				if (channel->backends[nbackends]) {
					nbackends++;
					channel->backends[nbackends] = NULL;
				}
			} else
				free(settings);
			nsettings = 0;
			settings = NULL;
		}
		if (*end == '\0') {
			break;
		}
	}
}

void
netresolve_set_default_loopback(netresolve_t channel, bool value)
{
	channel->request.default_loopback = value;
}

void
netresolve_set_dns_srv_lookup(netresolve_t channel, bool value)
{
	channel->request.dns_srv_lookup = value;
}

void
netresolve_set_family(netresolve_t channel, int family)
{
	channel->request.family = family;
}

void
netresolve_set_socktype(netresolve_t channel, int socktype)
{
	channel->request.socktype = socktype;
}

void
netresolve_set_protocol(netresolve_t channel, int protocol)
{
	channel->request.protocol = protocol;
}

void
netresolve_set_success_callback(netresolve_t channel,
		netresolve_callback_t on_success, void *user_data)
{
	channel->callbacks.on_success = on_success;
	channel->callbacks.user_data = user_data;
}

void
netresolve_set_fd_callback(netresolve_t channel,
		netresolve_fd_callback_t watch_fd,
		void *user_data)
{
	channel->callbacks.watch_fd = watch_fd;
	channel->callbacks.user_data_fd = user_data;
}

void
netresolve_set_bind_callback(netresolve_t channel,
		netresolve_socket_callback_t on_bind,
		void *user_data)
{
	channel->callbacks.on_bind = on_bind;
	channel->callbacks.on_connect = NULL;
	channel->callbacks.user_data_sock = user_data;

	netresolve_set_default_loopback(channel, false);
}

void
netresolve_set_connect_callback(netresolve_t channel,
		netresolve_socket_callback_t on_connect,
		void *user_data)
{
	channel->callbacks.on_bind = NULL;
	channel->callbacks.on_connect = on_connect;
	channel->callbacks.user_data_sock = user_data;

	netresolve_set_default_loopback(channel, true);
}

