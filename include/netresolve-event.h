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
#ifndef NETRESOLVE_EVENT_H
#define NETRESOLVE_EVENT_H

#include <netresolve-nonblock.h>
#include <event2/event.h>
#include <poll.h>
#include <stdlib.h>
#include <assert.h>

/* FIXME: This header file should be turned in a library to avoid symbol name clashes. */

struct netresolve_event_source {
	netresolve_t context;
	struct event *event;
	netresolve_watch_t source;
};

static int
condition_to_events(short condition)
{
	int events = 0;

	if (condition & EV_READ)
		events |= POLLIN;
	if (condition & EV_WRITE)
		events |= POLLOUT;

	return events;
}

static short
events_to_condition(int events)
{
	short condition = EV_PERSIST;

	if (events & POLLIN)
		condition |= EV_READ;
	if (events & POLLOUT)
		condition |= EV_WRITE;

	return condition;
}

static void
handler(int fd, short condition, void *data)
{
	struct netresolve_event_source *event_source = data;

	netresolve_dispatch(event_source->context, event_source->source, condition_to_events(condition));
}

static void*
add_watch(netresolve_t context, int fd, int events, netresolve_watch_t source)
{
	struct event_base *base = netresolve_get_user_data(context);
	struct netresolve_event_source *event_source = calloc(1, sizeof *event_source);

	assert(event_source);

	event_source->context = context;
	event_source->event = event_new(base, fd, events_to_condition(events), handler, event_source);
	event_source->source = source;

	assert(event_source->event);

	event_add(event_source->event, NULL);

	return event_source;
}

static void
remove_watch(netresolve_t context, int fd, void *handle)
{
	struct netresolve_event_source *event_source = handle;

	event_free(event_source->event);
	free(event_source);
}

__attribute__((unused))
static netresolve_t
netresolve_event_new(struct event_base *base)
{
	netresolve_t context = netresolve_context_new();

	if (context)
		netresolve_set_fd_callbacks(context, add_watch, remove_watch, NULL, base);

	return context;
}

#endif /* NETRESOLVE_EVENT_H */
