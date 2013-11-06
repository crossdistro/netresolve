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
#ifndef NETRESOLVE_COMMON_H
#define NETRESOLVE_COMMON_H

typedef enum netresolve_flag {
	/* NETRESOLVE_FLAG_DEFAULT_LOOPBACK:
	 *
	 * Translate `NULL` to `127.0.0.1` and `::1` instead of `0.0.0.0` and
	 * `::`. Implemented together by `any` and `loopback`. Passed reversed
	 * as `AI_PASSIVE` to `getaddrinfo()`.
	 */
	NETRESOLVE_FLAG_DEFAULT_LOOPBACK, // reversed AI_PASSIVE
	// NETRESOLVE_FLAG_ADDRCONFIG,
	// NETRESOLVE_FLAG_NUMERICHOST,
	// AI_V4MAPPED, AI_ALL
	// NETRESOLVE_FLAG_NUMERICSERV,
	// NETRESOLVE_FLAG_CANONNAME,
	_NETRESOLVE_FLAG_COUNT
} netresolve_flag_t;

#endif /* NETRESOLVE_COMMON_H */
