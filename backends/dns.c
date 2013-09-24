#include <ares.h>

#include <netresolve-backend.h>

/* A timeout starting when the first successful answer has been received. */
static int partial_timeout = 5;

typedef struct {
	netresolve_backend_t resolver;
	ares_channel channel;
	fd_set rfds;
	fd_set wfds;
	int nfds;
	int ptfd;
} Data;

void
register_fds(Data *data)
{
	netresolve_backend_t resolver = data->resolver;
	fd_set rfds;
	fd_set wfds;
	int nfds, fd;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	nfds = ares_fds(data->channel, &rfds, &wfds);

	for (fd = 0; fd < nfds || fd < data->nfds; fd++) {
		if (!FD_ISSET(fd, &rfds) && FD_ISSET(fd, &data->rfds)) {
			FD_CLR(fd, &data->rfds);
			netresolve_backend_watch_fd(resolver, fd, 0);
		} else if (FD_ISSET(fd, &rfds) && !FD_ISSET(fd, &data->rfds)) {
			FD_SET(fd, &data->rfds);
			netresolve_backend_watch_fd(resolver, fd, POLLIN);
		}
		if (!FD_ISSET(fd, &wfds) && FD_ISSET(fd, &data->wfds)) {
			FD_CLR(fd, &data->wfds);
			netresolve_backend_watch_fd(resolver, fd, 0);
		} else if (FD_ISSET(fd, &wfds) && !FD_ISSET(fd, &data->wfds)) {
			FD_SET(fd, &data->wfds);
			netresolve_backend_watch_fd(resolver, fd, POLLOUT);
		}
	}

	data->nfds = nfds;

	if (!nfds)
		netresolve_backend_finished(resolver);
}

void
host_callback(void *arg, int status, int timeouts, struct hostent *he)
{
	Data *data = arg;
	netresolve_backend_t resolver = data->resolver;

	switch (status) {
	case ARES_EDESTRUCTION:
		break;
	case ARES_SUCCESS:
		data->ptfd = netresolve_backend_watch_timeout(resolver, partial_timeout, 0);
		if (data->ptfd == -1)
			error("timer: %s", strerror(errno));
		netresolve_backend_apply_hostent(resolver, he, false);
		break;
	default:
		error("ares: %s\n", ares_strerror(status));
	}
}

void
start(netresolve_backend_t resolver, char **settings)
{
	Data *data = netresolve_backend_new_data(resolver, sizeof *data);
	const char *node = netresolve_backend_get_node(resolver);
	int family = netresolve_backend_get_family(resolver);
	int status;

	if (!data)
		goto fail;

	data->resolver = resolver;
	data->ptfd = -1;

	FD_ZERO(&data->rfds);
	FD_ZERO(&data->wfds);

	status = ares_library_init(ARES_LIB_INIT_ALL);
	if (status != ARES_SUCCESS)
		goto fail_ares;
	status = ares_init(&data->channel);
	if (status != ARES_SUCCESS)
		goto fail_ares;

	if (family == AF_INET || family == AF_UNSPEC)
		ares_gethostbyname(data->channel, node, AF_INET, host_callback, data);
	if (family == AF_INET6 || family == AF_UNSPEC)
		ares_gethostbyname(data->channel, node, AF_INET6, host_callback, data);
	register_fds(data);

	return;

fail_ares:
	error("ares: %s\n", ares_strerror(status));
fail:
	netresolve_backend_failed(resolver);
}

void
dispatch(netresolve_backend_t resolver, int fd, int events)
{
	Data *data = netresolve_backend_get_data(resolver);

	int rfd = events & POLLIN ? fd : ARES_SOCKET_BAD;
	int wfd = events & POLLOUT ? fd : ARES_SOCKET_BAD;

	if (fd == data->ptfd) {
		error("partial response used due to a timeout\n");
		netresolve_backend_finished(resolver);
		return;
	}

	ares_process_fd(data->channel, rfd, wfd);
	register_fds(data);
}

void
cleanup(netresolve_backend_t resolver)
{
	Data *data = netresolve_backend_get_data(resolver);
	int fd;

	for (fd = 0; fd < data->nfds; fd++) {
		if (FD_ISSET(fd, &data->rfds) || FD_ISSET(fd, &data->wfds)) {
			FD_CLR(fd, &data->rfds);
			netresolve_backend_watch_fd(resolver, fd, 0);
		}
	}

	if (data->ptfd != -1)
		netresolve_backend_drop_timeout(resolver, data->ptfd);

	ares_destroy(data->channel);
	//ares_library_cleanup();
}
