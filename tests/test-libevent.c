/* Copyright (c) 2013-2014 Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
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
#include <event2/event.h>

/* FIXME: Improve the API so that this is not needed! */
#include "netresolve-private.h"

#include "common.h"

static void
handler(int fd, short events, void *user_data)
{
	netresolve_t channel = user_data;

	netresolve_dispatch_fd(channel, fd, 0x01);
}

static void
watch_fd(netresolve_query_t query, int fd, int events, void *user_data)
{
	struct event_base *base = user_data;
	netresolve_t channel = query->channel;

	/* FIXME: Improve the API so that this is not needed! */
	static struct event *event = NULL;

	if (events)
		event = event_new(base, fd, EV_READ | EV_TIMEOUT, handler, channel);
	else {
		event_free(event);
		event = NULL;
	}
}

static void
on_success(netresolve_query_t query, void *user_data)
{
	int which = *(int *) netresolve_query_get_user_data(query);
	int *count = user_data;

	switch (which) {
	case 1:
		check_address(query, AF_INET6, "1:2:3:4:5:6:7:8", 999999);
		break;
	case 2:
		check_address(query, AF_INET, "1.2.3.4", 999999);
		break;
	default:
		abort();
	}

	(*count)++;
}

int
main(int argc, char **argv)
{
	struct event_base *base;
	netresolve_t channel;
	netresolve_query_t query1, query2;
	int finished = 0;
	const char *node1 = "1:2:3:4:5:6:7:8%999999";
	const char *node2 = "1.2.3.4%999999";
	int data1 = 1;
	int data2 = 2;
	const char *service = "80";
	int family = AF_UNSPEC;
	int socktype = 0;
	int protocol = IPPROTO_TCP;

	/* Create an event base. */
	base = event_base_new();
	if (!base) {
		perror("event_base_new");
		abort();
	}

	/* Create a channel. */
	channel = netresolve_open();
	if (!channel) {
		perror("netresolve_open");
		abort();
	}

	/* Set callbacks. */
	netresolve_set_fd_callback(channel, watch_fd, base);
	netresolve_set_success_callback(channel, on_success, &finished);

	/* Resolver configuration. */
	netresolve_set_family(channel, family);
	netresolve_set_socktype(channel, socktype);
	netresolve_set_protocol(channel, protocol);

	/* Start name resolution. */
	netresolve_set_user_data(channel, &data1);
	query1 = netresolve_query(channel, node1, service);
	netresolve_set_user_data(channel, &data2);
	query2 = netresolve_query(channel, node2, service);

	assert(query1 && query2);
	assert(netresolve_query_get_user_data(query1) == &data1);
	assert(netresolve_query_get_user_data(query2) == &data2);

	/* Run the main loop. */
	event_base_dispatch(base);

	/* Clean up. */
	netresolve_close(channel);
	event_base_free(base);

	exit(EXIT_SUCCESS);
}
