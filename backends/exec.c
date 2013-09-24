#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>

#include <netresolve-backend.h>
#include <netresolve-string.h>

typedef struct {
	char *buffer;
	char *start;
	char *end;
} Buffer;

typedef struct {
	int pid;
	Buffer inbuf;
	int infd;
	Buffer outbuf;
	int outfd;
} Data;

static bool
start_subprocess(char *const command[], int *pid, int *infd, int *outfd)
{
	int p1[2], p2[2];

	if (!pid || !infd || !outfd)
		return false;

	if (pipe(p1) == -1)
		goto err_pipe1;
	if (pipe(p2) == -1)
		goto err_pipe2;
	if ((*pid = fork()) == -1)
		goto err_fork;

	if (*pid) {
		*infd = p1[1];
		*outfd = p2[0];
		close(p1[0]);
		close(p2[1]);
		return true;
	} else {
		dup2(p1[0], 0);
		dup2(p2[1], 1);
		close(p1[1]);
		close(p2[0]);
		execvp(*command, command);
		/* Subprocess error occured. */
		fprintf(stderr, "error running %s: %s\n", *command, strerror(errno));
		abort();
	}

err_fork:
	close(p2[1]);
	close(p2[0]);
err_pipe2:
	close(p1[1]);
	close(p1[0]);
err_pipe1:
	return false;
}

static void
send_stdin(netresolve_backend_t resolver, Data *data)
{
	ssize_t size;

	if (data->inbuf.start == data->inbuf.end) {
		netresolve_backend_watch_fd(resolver, data->infd, 0);
		return;
	}
	size = write(data->infd, data->inbuf.start, data->inbuf.end - data->inbuf.start);
	if (size <= 0) {
		abort();
		debug("write failed: %s", strerror(errno));
		netresolve_backend_failed(resolver);
		return;
	}
	data->inbuf.start += size;
	return;
}

static bool
received_line(netresolve_backend_t resolver, Data *data, const char *line)
{
	char addrprefix[] = "address ";
	int addrprefixlen = strlen(addrprefix);

	debug("received: %s\n", data->outbuf.buffer);

	if (!*line)
		return true;

	if (!strncmp(addrprefix, line, addrprefixlen)) {
		Address address;
		int family;
		int ifindex;

		if (netresolve_backend_parse_address(line + addrprefixlen, &address, &family, &ifindex))
			netresolve_backend_add_address(resolver, family, &address, ifindex);
	}

	return false;
}

static void
pickup_stdout(netresolve_backend_t resolver, Data *data)
{
	char *nl;
	int size;

	if (!data->outbuf.buffer) {
		data->outbuf.buffer = data->outbuf.start = malloc(1024);
		if (data->outbuf.buffer)
			data->outbuf.end = data->outbuf.start + 1024;
	}
	if (!data->outbuf.buffer) {
		netresolve_backend_failed(resolver);
		return;
	}

	size = read(data->outfd, data->outbuf.start, data->outbuf.end - data->outbuf.start);
	if (size <= 0) {
		abort();
		if (size < 0)
			error("read: %s", strerror(errno));
		netresolve_backend_failed(resolver);
		return;
	}
	debug("read: %*s\n", size, data->outbuf.start);
	data->outbuf.start += size;

	while ((nl = memchr(data->outbuf.buffer, '\n', data->outbuf.start - data->outbuf.buffer))) {
		*nl++ = '\0';
		if (received_line(resolver, data, data->outbuf.buffer)) {
			netresolve_backend_finished(resolver);
			return;
		}
		memmove(data->outbuf.buffer, nl, data->outbuf.end - data->outbuf.start);
		data->outbuf.start = data->outbuf.buffer + (data->outbuf.start - nl);
	}
}

void
start(netresolve_backend_t resolver, char **settings)
{
	Data *data = netresolve_backend_new_data(resolver, sizeof *data);

	if (!data || !start_subprocess(settings, &data->pid, &data->infd, &data->outfd)) {
		netresolve_backend_failed(resolver);
		return;
	}

	data->inbuf.buffer = strdup(netresolve_get_request_string(resolver));
	data->inbuf.start = data->inbuf.buffer;
	data->inbuf.end = data->inbuf.buffer + strlen(data->inbuf.buffer);

	netresolve_backend_watch_fd(resolver, data->infd, POLLOUT);
	netresolve_backend_watch_fd(resolver, data->outfd, POLLIN);
}

void
dispatch(netresolve_backend_t resolver, int fd, int events)
{
	Data *data = netresolve_backend_get_data(resolver);

	debug("events %d on fd %d\n", events, fd);

	if (fd == data->infd && events & POLLOUT)
		send_stdin(resolver, data);
	else if (fd == data->outfd && events & POLLIN) {
		pickup_stdout(resolver, data);
	} else if (fd == data->outfd && events & POLLHUP) {
		error("incomplete request\n");
		netresolve_backend_failed(resolver);
	} else {
		error("unknown events %d on fd %d\n", events, fd);
		netresolve_backend_failed(resolver);
	}
}

void
cleanup(netresolve_backend_t resolver)
{
	Data *data = netresolve_backend_get_data(resolver);

	netresolve_backend_watch_fd(resolver, data->infd, 0);
	netresolve_backend_watch_fd(resolver, data->outfd, 0);
	close(data->infd);
	close(data->outfd);
	/* TODO: Implement proper child handling. */
	kill(data->pid, SIGKILL);
	free(data->inbuf.buffer);
	free(data->outbuf.buffer);
}
