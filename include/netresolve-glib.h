/* Copyright (c) 2013-2014 Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
 * All rights reserved.
 *
 * Redistribution and use in glib_source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of glib_source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO GLIB SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef NETRESOLVE_GLIB_H
#define NETRESOLVE_GLIB_H

#include <netresolve-nonblock.h>
#include <glib.h>
#include <poll.h>
#include <stdlib.h>
#include <assert.h>

/* FIXME: This header file should be turned in a library to avoid symbol name clashes. */

struct netresolve_glib_source {
	netresolve_t context;
	GIOChannel *stream;
	netresolve_source_t source;
};

static int
condition_to_events(int condition)
{
	int events = 0;

	if (condition & G_IO_IN)
		events |= POLLIN;
	if (condition & G_IO_OUT)
		events |= POLLOUT;

	return events;
}

static int
events_to_condition(int events)
{
	GIOCondition condition = 0;

	if (events & POLLIN)
		condition |= G_IO_IN;
	if (events & POLLOUT)
		condition |= G_IO_OUT;

	return condition;
}

static gboolean
handler(GIOChannel *stream, GIOCondition condition, gpointer data)
{
	struct netresolve_glib_source *glib_source = data;

	if (!netresolve_dispatch(glib_source->context, glib_source->source, condition_to_events(condition)))
		abort();

	return TRUE;
}

static void*
watch_fd(netresolve_t context, int fd, int events, netresolve_source_t source)
{
	struct netresolve_glib_source *glib_source = calloc(1, sizeof *glib_source);

	assert(glib_source);

	glib_source->context = context;
	glib_source->stream = g_io_channel_unix_new(fd);
	glib_source->source = source;

	g_io_add_watch(glib_source->stream, events_to_condition(events), handler, glib_source);

	return glib_source;
}

static void
unwatch_fd(netresolve_t context, int fd, void *handle)
{
	struct netresolve_glib_source *glib_source = handle;

	g_io_channel_unref(glib_source->stream);
	free(glib_source);
}

__attribute__((unused))
static netresolve_t 
netresolve_glib_new(void)
{
	netresolve_t context = netresolve_context_new();

	assert(context);

	netresolve_set_fd_callbacks(context, watch_fd, unwatch_fd, NULL, NULL);

	return context;
}

#endif /* NETRESOLVE_GLIB_H */
