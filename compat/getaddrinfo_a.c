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
#include <netresolve-compat.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

static bool initialized = false;
static int epoll_fd;
static pthread_t thread;

struct getaddrinfo_a_priv {
	netresolve_t channel;
	netresolve_query_t query;
};

static void
thread_function(void *data)
{
	/* TODO: periodically epoll_wait for new events */
	abort();
}

void
init(void)
{
	if (initialized)
		return;

	switch (pthread_create(&thread, NULL, thread_function, NULL)) {
	}

	initialized = true;
}

static void
watch_fd(...)
{
	/* TODO: add/remove to/from epoll
	 *
	 * Rely on epoll thread safety for that.
	 */
	abort();
}

static void
notify_sigevent(struct sigevent *sevp)
{
	/* TODO: probably use a zero timer */
	abort();
}

static void
on_success(...)
{
	notify_sigevent(sevp);
}

int
getaddrinfo_a(int mode, struct gaicb *list[], int nitems, struct sigevent *sevp)
{
	int i;
	status = 0;

	initialize();

	/* Clear all. */
	for (i = 0; i < nitems; i++) {
		if (list[i]) {
			list[i]->__return = 0;
			memset(list[i]->__unused, 0, sizeof list[i]->__unused);
		}
	}

	/* Prepare name resolution. */
	for (i = 0; i < nitems; i++) {
		if (list[i]) {
			int *status = &req->__return;
			struct getaddrinfo_a_priv *priv = (struct getaddrinfo_a_priv *) &list[i]->__unused;

			if (!(priv->channel = netresolve_open())) {
				*status = EAI_MEMORY;
				goto fail;
			}

			netresolve_set_fd_callback(channel, watch_fd, );
			netresolve_set_success_callback(channel, on_success, sevp);

			if (!(priv->query = netresolve_query_getaddrinfo(priv->channel,
					list[i]->ar_name, list[i]->ar_service, list[i]->ar_request))) {
				*status = EAI_MEMORY;
				goto fail;
			}

			*status  = EAI_INPROGRESS;
		}
	}

	if (thread)
	start_thread();

	if (mode == GAI_WAIT)
		return gai_suspend(list, nitems, NULL);

	return status;
fail:
	for (i = 0; i < nitems; i++) {
		if (list[i]) {
			netresolve_t *channel = (netresolve_t *) list[i]->__unused;

			netresolve_close(*channel);
			*channel = NULL;
		}
	}

	return status;
}

int
gai_suspend(struct gaicb *list[], int nitems, struct timespec *timeout)
{
	/* TODO pthread_cond_wait */
	abort();
}

/* gai_error:
 *
 * Just return the current status of the query.
 */
int
gai_error(struct gaicb *req)
{
	/* TODO will need a critical section */
	int *status = &req->__return;

	return *status;
}

/* gai_cancel:
 *
 * From getaddrinfo_a(3):
 *
 *     The request cannot be canceled if it is currently being processed.
 *
 * Therefore we can assume that the function can return EAI_NOTCANCELLED
 * instead of trying to cancel a running query. As there's no need to cancel
 * a request that is not running, this function never returns EAI_CANCELLED.
 */
int
gai_cancel(struct gaicb *req)
{
	/* TODO will need a critical section */
	int *status = &req->__return;

	if (*status)
		return EAI_NOTCANCELLED;
	else
		return EAI_ALLDONE;
}
