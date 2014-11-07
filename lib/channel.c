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
getenv_bool(const char *name, bool def)
{
	const char *value = secure_getenv(name);

	return value ? (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "1")) : def;
}

static int
getenv_int(const char *name, int def)
{
	const char *value = secure_getenv(name);

	return value ? strtoll(value, NULL, 10) : def;
}

static int
getenv_family(const char *name, int def)
{
	const char *value = secure_getenv(name);

	return value ? netresolve_family_from_string(value) : def;
}

netresolve_t
netresolve_open(void)
{
	/* FIXME: this should probably be only called once */
	netresolve_set_log_level(getenv_bool("NETRESOLVE_VERBOSE", false) ? NETRESOLVE_LOG_LEVEL_DEBUG : NETRESOLVE_LOG_LEVEL_QUIET);

	netresolve_t channel = calloc(1, sizeof *channel);
	if (!channel)
		return NULL;

	channel->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (channel->epoll_fd == -1) {
		free(channel);
		return NULL;
	}

	channel->config.force_family = getenv_family("NETRESOLVE_FORCE_FAMILY", AF_UNSPEC);

	channel->request.default_loopback = getenv_bool("NETRESOLVE_FLAG_DEFAULT_LOOPBACK", false);
	channel->request.clamp_ttl = getenv_int("NETRESOLVE_CLAMP_TTL", -1);
	channel->request.timeout = getenv_int("NETRESOLVE_TIMEOUT", 15000);
	channel->request.partial_timeout = getenv_int("NETRESOLVE_PARTIAL_TIMEOUT", 5000);

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
	assert(fd >= 0);
	assert(events);

	debug_channel(channel, "dispatching: fd=%d events=%d", fd, events);

	for (int i = 0; i < channel->nqueries; i++) {
		netresolve_query_t query = channel->queries[i];

		if (netresolve_query_dispatch(query, fd, events))
			return true;
	}

	return false;
}

bool
netresolve_epoll(netresolve_t channel, bool block)
{
	static const int maxevents = 10;
	struct epoll_event events[maxevents];
	int nevents;
	int i;

	if (channel->epoll_count > 0) {
		debug_channel(channel, "running epoll...");

		nevents = epoll_wait(channel->epoll_fd, events, maxevents, block ? -1 : 0);
		if (nevents == -1) {
			error("epoll_wait: %s", strerror(errno));
			return false;
		}
		for (i = 0; i < nevents; i++)
			dispatch_fd(channel, events[i].data.fd, events[i].events);
	}

	return true;
}

void
netresolve_watch_fd(netresolve_t channel, int fd, int events)
{
	assert (fd >= 0);
	assert (events);
	assert (!(events & ~(EPOLLIN | EPOLLOUT)));

	struct epoll_event event = { .events = events, .data = { .fd = fd} };

	if (epoll_ctl(channel->epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
		error("epoll_ctl: %s (fd=%d)", strerror(errno), fd);
		abort();
	}

	channel->epoll_count++;

	if (channel->epoll_count == 1 && channel->fd_callbacks.watch_fd)
		channel->epoll_handle = channel->fd_callbacks.watch_fd(channel, channel->epoll_fd, POLLIN, channel, channel->fd_callbacks.user_data);

	debug_channel(channel, "added file descriptor: fd=%d events=%d (total %d)", fd, events, channel->epoll_count);
}

void
netresolve_unwatch_fd(netresolve_t channel, int fd)
{
	assert (fd >= 0);

	assert(channel->epoll_count > 0);

	if (epoll_ctl(channel->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
		error("epoll_ctl: %s (fd=%d}", strerror(errno), fd);
		abort();
	}

	channel->epoll_count--;

	if (channel->epoll_count == 0 && channel->fd_callbacks.watch_fd)
		channel->fd_callbacks.unwatch_fd(channel, channel->epoll_fd, channel->epoll_handle, channel->fd_callbacks.user_data);

	debug_channel(channel, "removed file descriptor: fd=%d (total %d)", fd, channel->epoll_count);
}

int
netresolve_add_timeout(netresolve_t channel, time_t sec, long nsec)
{
	int fd;
	struct itimerspec timerspec = {{0, 0}, {sec, nsec}};

	if ((fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1)
		return -1;

	if (timerfd_settime(fd, 0, &timerspec, NULL) == -1) {
		close(fd);
		return -1;
	}

	debug_channel(channel, "adding timeout: fd=%d sec=%d nsec=%ld", fd, (int) sec, nsec);

	netresolve_watch_fd(channel, fd, POLLIN);

	return fd;
}

int
netresolve_add_timeout_ms(netresolve_t channel, time_t msec)
{
	return netresolve_add_timeout(channel, msec / 1000, (msec % 1000) * 1000000L);
}

void
netresolve_remove_timeout(netresolve_t channel, int fd)
{
	netresolve_unwatch_fd(channel, fd);
	debug_channel(channel, "removed timeout: fd=%d", fd);
	close(fd);
}

static netresolve_query_t
start_query(netresolve_t channel, netresolve_query_t query)
{
	netresolve_query_start(query);

	/* Wait for the channel in blocking mode. */
	if (!channel->fd_callbacks.watch_fd)
		while (channel->epoll_count > 0)
			if (!netresolve_epoll(channel, true))
				abort();

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

	return start_query(channel, query);
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

	return start_query(channel, query);
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

	return start_query(channel, query);
}

bool
netresolve_dispatch_fd(netresolve_t channel, void *data, int events)
{
	if (data != channel) {
		errno = EINVAL;
		return false;
	}
	if (events != EPOLLIN) {
		errno = EINVAL;
		return false;
	}

	return netresolve_epoll(channel, false);
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
netresolve_set_fd_callbacks(netresolve_t channel, const struct netresolve_fd_callbacks *callbacks)
{
	memcpy(&channel->fd_callbacks, callbacks, sizeof *callbacks);
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

