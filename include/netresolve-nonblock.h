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
#ifndef NETRESOLVE_NONBLOCK_H
#define NETRESOLVE_NONBLOCK_H

#include <netresolve.h>

typedef struct netresolve_watch *netresolve_watch_t;

typedef void *(*netresolve_watch_add_callback_t)(netresolve_t context, int fd, int events, netresolve_watch_t source);
typedef void (*netresolve_watch_remove_callback_t)(netresolve_t context, int fd, void *handle);
typedef void (*netresolve_free_user_data_callback_t)(void *user_data);

void netresolve_set_fd_callbacks(netresolve_t context,
		netresolve_watch_add_callback_t add_watch,
		netresolve_watch_remove_callback_t remove_watch,
		void *user_data,
		netresolve_free_user_data_callback_t free_user_data);
void *netresolve_get_user_data(netresolve_t context);
bool netresolve_dispatch(netresolve_t context, netresolve_watch_t source, int events);

#endif /* NETRESOLVE_NONBLOCK_H */
