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
#include <stdio.h>
#include <errno.h>

#include <netresolve.h>
#include <netresolve-cli.h>
#include <netresolve-string.h>

int
main(int argc, char **argv)
{
	netresolve_t resolver;
	int status;

	resolver = netresolve_open();
	if (!resolver) {
		fprintf(stderr, "netresolve: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	status = netresolve_resolve_argv(resolver, argv + 1);
	if (status) {
		fprintf(stderr, "netresolve: %s\n", strerror(status));
		return EXIT_FAILURE;
	}

	printf("%s", netresolve_get_response_string(resolver));

	netresolve_close(resolver);
	return EXIT_SUCCESS;
}
