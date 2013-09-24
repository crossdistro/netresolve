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
