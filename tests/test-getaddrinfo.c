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
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int
main(int argc, char **argv)
{
	const char *node = "1:2:3:4:5:6:7:8%999999";
	const char *service = "80";
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = 0,
		.ai_protocol = IPPROTO_TCP,
		.ai_flags = 0,
	};
	struct addrinfo *result = NULL;
	int status;

	status = getaddrinfo(node, service, &hints, &result);
	assert(status == 0);
	assert(result);
	assert(!result->ai_next);
}
