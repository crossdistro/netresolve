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
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/timerfd.h>

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
	netresolve_t channel;

	/* FIXME: this should probably be only called once */
	netresolve_set_log_level(getenv_bool("NETRESOLVE_VERBOSE", false) ? NETRESOLVE_LOG_LEVEL_DEBUG : NETRESOLVE_LOG_LEVEL_QUIET);

	if (!(channel = calloc(1, sizeof *channel)))
		return NULL;

	channel->sources.previous = channel->sources.next = &channel->sources;

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
	if (!channel)
		return;

	for (int i = 0; i < channel->nqueries; i++)
		netresolve_query_done(channel->queries[i]);
	free(channel->queries);

	netresolve_set_backend_string(channel, "");
	if (channel->epoll.fd != -1 && close(channel->epoll.fd) == -1)
		abort();
	if (channel->callbacks.free_user_data)
		channel->callbacks.free_user_data(channel->callbacks.user_data);
	memset(channel, 0, sizeof *channel);
	free(channel);
}

void
netresolve_watch_fd(netresolve_t channel, int fd, int events)
{
	struct netresolve_source *source;

	assert(fd >= 0);
	assert(events && !(events & ~(POLLIN | POLLOUT)));

	if (!(source = calloc(1, sizeof(*source))))
		abort();

	source->fd = fd;
	source->handle = channel->callbacks.watch_fd(channel, fd, events, source);

	source->previous = channel->sources.previous;
	source->next = &channel->sources;
	source->previous->next = source;
	source->next->previous = source;

	channel->count++;

	debug_channel(channel, "added file descriptor: fd=%d events=%d (total %d)", fd, events, channel->count);
}

void
netresolve_unwatch_fd(netresolve_t channel, int fd)
{
	struct netresolve_source *source;

	assert(fd >= 0);

	for (source = channel->sources.next; source != &channel->sources; source = source->next)
		if (source->fd == fd)
			break;

	assert(channel->count > 0);
	assert(source != &channel->sources);

	channel->callbacks.unwatch_fd(channel, fd, source->handle);

	source->previous->next = source->next;
	source->next->previous = source->previous;
	free(source);

	channel->count--;

	debug_channel(channel, "removed file descriptor: fd=%d (total %d)", fd, channel->count);
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
	close(fd);

	debug_channel(channel, "removed timeout: fd=%d", fd);
}

static netresolve_query_t
start_query(netresolve_t channel, netresolve_query_t query)
{
	/* Install default callbacks for first query in blocking mode. */
	if (!channel->callbacks.watch_fd)
		netresolve_epoll_install(channel, &channel->epoll, NULL);

	netresolve_query_setup(query);

	/* Wait for the channel in blocking mode. */
	if (channel->callbacks.user_data == &channel->epoll)
		netresolve_epoll_wait(channel);

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
netresolve_dispatch(netresolve_t channel, void *data, int events)
{
	struct netresolve_source *source = data;

	assert(source);
	assert(events & (POLLIN | POLLOUT));
	assert(!(events & ~(POLLIN | POLLOUT)));

	debug_channel(channel, "dispatching: fd=%d events=%d", source->fd, events);

	for (int i = 0; i < channel->nqueries; i++) {
		netresolve_query_t query = channel->queries[i];

		if (netresolve_query_dispatch(query, source->fd, events))
			return true;
	}

	return false;
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
netresolve_set_fd_callbacks(netresolve_t channel, netresolve_watch_fd_callback_t watch_fd, netresolve_unwatch_fd_callback_t unwatch_fd)
{
	assert(watch_fd && unwatch_fd);
	assert(!channel->callbacks.watch_fd && !channel->callbacks.unwatch_fd);

	channel->callbacks.watch_fd = watch_fd;
	channel->callbacks.unwatch_fd = unwatch_fd;
}

void
netresolve_set_user_data(netresolve_t channel,
		void *user_data,
		netresolve_free_user_data_callback_t free_user_data)
{
	assert(!channel->callbacks.user_data);

	channel->callbacks.user_data = user_data;
	channel->callbacks.free_user_data = free_user_data;
}

void *
netresolve_get_user_data(netresolve_t channel)
{
	return channel->callbacks.user_data;
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

