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
#include <ares.h>

#include <netresolve-backend.h>

/* A timeout starting when the first successful answer has been received. */
static int partial_timeout = 5;

struct priv_dns {
	ares_channel channel;
	fd_set rfds;
	fd_set wfds;
	int nfds;
	int ptfd;
};

void
register_fds(netresolve_backend_t resolver)
{
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);
	fd_set rfds;
	fd_set wfds;
	int nfds, fd;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	nfds = ares_fds(priv->channel, &rfds, &wfds);

	for (fd = 0; fd < nfds || fd < priv->nfds; fd++) {
		if (!FD_ISSET(fd, &rfds) && FD_ISSET(fd, &priv->rfds)) {
			FD_CLR(fd, &priv->rfds);
			netresolve_backend_watch_fd(resolver, fd, 0);
		} else if (FD_ISSET(fd, &rfds) && !FD_ISSET(fd, &priv->rfds)) {
			FD_SET(fd, &priv->rfds);
			netresolve_backend_watch_fd(resolver, fd, POLLIN);
		}
		if (!FD_ISSET(fd, &wfds) && FD_ISSET(fd, &priv->wfds)) {
			FD_CLR(fd, &priv->wfds);
			netresolve_backend_watch_fd(resolver, fd, 0);
		} else if (FD_ISSET(fd, &wfds) && !FD_ISSET(fd, &priv->wfds)) {
			FD_SET(fd, &priv->wfds);
			netresolve_backend_watch_fd(resolver, fd, POLLOUT);
		}
	}

	priv->nfds = nfds;

	if (!nfds)
		netresolve_backend_finished(resolver);
}

void
host_callback(void *arg, int status, int timeouts, struct hostent *he)
{
	netresolve_backend_t resolver = arg;
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);
	int socktype = -1;
	int protocol = -1;
	int port = -1;
	int priority = 0;
	int weight = 0;

	switch (status) {
	case ARES_EDESTRUCTION:
		break;
	case ARES_SUCCESS:
		priv->ptfd = netresolve_backend_watch_timeout(resolver, partial_timeout, 0);
		if (priv->ptfd == -1)
			error("timer: %s", strerror(errno));
		netresolve_backend_apply_hostent(resolver, he, socktype, protocol, port, priority, weight);
		break;
	default:
		error("ares: %s\n", ares_strerror(status));
	}
}

void
start(netresolve_backend_t resolver, char **settings)
{
	struct priv_dns *priv = netresolve_backend_new_priv(resolver, sizeof *priv);
	const char *node = netresolve_backend_get_node(resolver);
	int family = netresolve_backend_get_family(resolver);
	int status;

	if (!priv)
		goto fail;

	priv->ptfd = -1;

	FD_ZERO(&priv->rfds);
	FD_ZERO(&priv->wfds);

	status = ares_library_init(ARES_LIB_INIT_ALL);
	if (status != ARES_SUCCESS)
		goto fail_ares;
	status = ares_init(&priv->channel);
	if (status != ARES_SUCCESS)
		goto fail_ares;

	if (family == AF_INET || family == AF_UNSPEC)
		ares_gethostbyname(priv->channel, node, AF_INET, host_callback, resolver);
	if (family == AF_INET6 || family == AF_UNSPEC)
		ares_gethostbyname(priv->channel, node, AF_INET6, host_callback, resolver);
	register_fds(resolver);

	return;

fail_ares:
	error("ares: %s\n", ares_strerror(status));
fail:
	netresolve_backend_failed(resolver);
}

void
dispatch(netresolve_backend_t resolver, int fd, int events)
{
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);

	int rfd = events & POLLIN ? fd : ARES_SOCKET_BAD;
	int wfd = events & POLLOUT ? fd : ARES_SOCKET_BAD;

	if (fd == priv->ptfd) {
		error("partial response used due to a timeout\n");
		netresolve_backend_finished(resolver);
		return;
	}

	ares_process_fd(priv->channel, rfd, wfd);
	register_fds(resolver);
}

void
cleanup(netresolve_backend_t resolver)
{
	struct priv_dns *priv = netresolve_backend_get_priv(resolver);
	int fd;

	for (fd = 0; fd < priv->nfds; fd++) {
		if (FD_ISSET(fd, &priv->rfds) || FD_ISSET(fd, &priv->wfds)) {
			FD_CLR(fd, &priv->rfds);
			netresolve_backend_watch_fd(resolver, fd, 0);
		}
	}

	if (priv->ptfd != -1)
		netresolve_backend_drop_timeout(resolver, priv->ptfd);

	ares_destroy(priv->channel);
	//ares_library_cleanup();
}
