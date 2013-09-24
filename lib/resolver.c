#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <poll.h>
#include <sys/epoll.h>
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

void
netresolve_set_log_level(netresolve_t resolver, int level)
{
	resolver->log_level = level;
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

static void
load_backends(netresolve_t resolver, const char *string)
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

void
netresolve_set_backend_string(netresolve_t resolver, const char *string)
{
	load_backends(resolver, string);
}

void
_netresolve_set_state(netresolve_t resolver, enum netresolve_state state)
{
	/* Entering the initial state */
	if (resolver->state != NETRESOLVE_STATE_INIT && state == NETRESOLVE_STATE_INIT) {
		free(resolver->response.paths);
		free(resolver->response.canonname);
		memset(&resolver->response, 0, sizeof resolver->response);
	}

	/* Entering the waiting state. */
	if (resolver->state == NETRESOLVE_STATE_INIT && state == NETRESOLVE_STATE_WAIT
			&& resolver->callbacks.watch_fd)
		resolver->callbacks.watch_fd(resolver, resolver->epoll_fd, POLLIN, resolver->callbacks.user_data_fd);

	/* Leaving the waiting state. */
	if (resolver->state == NETRESOLVE_STATE_WAIT && state != NETRESOLVE_STATE_WAIT
			&& resolver->callbacks.watch_fd)
		resolver->callbacks.watch_fd(resolver, resolver->epoll_fd, 0, resolver->callbacks.user_data_fd);

	/* Notify about success. */
	if (state == NETRESOLVE_STATE_SUCCESS) {
		_netresolve_cleanup(resolver);
		/* Restart with the next *mandatory* backend if available. */
		while (*++resolver->backend) {
			if ((*resolver->backend)->mandatory) {
				_netresolve_start(resolver);
				return;
			}
		}
		if (resolver->callbacks.on_success)
			resolver->callbacks.on_success(resolver, resolver->callbacks.user_data);
	}

	/* Notify about failure. */
	if (state == NETRESOLVE_STATE_FAILURE) {
		_netresolve_cleanup(resolver);
		/* Restart with the next backend if available. */
		if (*++resolver->backend) {
			_netresolve_start(resolver);
			return;
		}
		if (resolver->callbacks.on_success)
			resolver->callbacks.on_success(resolver, resolver->callbacks.user_data);
	}

	resolver->state = state;
}

void
_netresolve_start(netresolve_t resolver)
{
	struct netresolve_backend *backend = *resolver->backend;

	/* Make sure a backend is loaded. */
	if (!backend || !backend->start) {
		_netresolve_set_state(resolver, NETRESOLVE_STATE_FAILURE);
		return;
	}

	/* Run the backend start function. */
	_netresolve_set_state(resolver, NETRESOLVE_STATE_WAIT);
	backend->start(resolver, backend->settings+1);
}

void
_netresolve_dispatch(netresolve_t resolver, int timeout)
{
	struct netresolve_backend *backend = *resolver->backend;
	static const int maxevents = 1;
	struct epoll_event events[maxevents];
	int nevents;
	int i;

	if (resolver->state != NETRESOLVE_STATE_WAIT)
		return;
	if (!backend || !backend->dispatch)
		goto err;

	nevents = epoll_wait(resolver->epoll_fd, events, maxevents, resolver->callbacks.watch_fd ? 0 : -1);
	if (nevents == -1)
		goto err;
	for (i = 0; resolver->state == NETRESOLVE_STATE_WAIT && i < nevents; i++)
		backend->dispatch(resolver, events[i].data.fd, events[i].events);

	return;
err:
	_netresolve_set_state(resolver, NETRESOLVE_STATE_FAILURE);
}

void
_netresolve_cleanup(netresolve_t resolver)
{
	struct netresolve_backend *backend = *resolver->backend;

	if (backend && backend->data) {
		backend->cleanup(resolver);
		free(backend->data);
		backend->data = NULL;
	}
}

static int
state_to_error(enum netresolve_state state)
{
	switch (state) {
	case NETRESOLVE_STATE_WAIT:
		return EWOULDBLOCK;
	case NETRESOLVE_STATE_SUCCESS:
		return 0;
	case NETRESOLVE_STATE_FAILURE:
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
	if (resolver->state == NETRESOLVE_STATE_WAIT)
		return EBUSY;
	_netresolve_set_state(resolver, NETRESOLVE_STATE_INIT);
	if (!resolver->backends)
		load_backends(resolver, getenv("NETRESOLVE_BACKENDS"));
	if (!resolver->backends)
		return ENODATA;
	resolver->backend = resolver->backends;

	resolver->request.node = node;
	resolver->request.service = service;
	resolver->request.family = family;
	resolver->request.socktype = socktype;
	resolver->request.protocol = protocol;

	/* Start network name resolution. */
	_netresolve_start(resolver);

	/* Blocking mode. */
	if (!resolver->callbacks.watch_fd)
		while (resolver->state == NETRESOLVE_STATE_WAIT)
			_netresolve_dispatch(resolver, -1);

	return state_to_error(resolver->state);
}

int
netresolve_dispatch(netresolve_t resolver, int fd, int events)
{
	if (fd != resolver->epoll_fd || events != EPOLLIN)
		return EINVAL;

	_netresolve_dispatch(resolver, 0);

	return state_to_error(resolver->state);
}

void
netresolve_cancel(netresolve_t resolver)
{
	_netresolve_set_state(resolver, NETRESOLVE_STATE_INIT);
}
