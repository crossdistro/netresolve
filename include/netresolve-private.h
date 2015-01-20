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
#ifndef NETRESOLVE_PRIVATE_H
#define NETRESOLVE_PRIVATE_H

#include <netresolve.h>
#include <netresolve-nonblock.h>
#include <netresolve-socket.h>
#include <netresolve-backend.h>
#include <netresolve-compat.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <nss.h>
#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <assert.h>

#define debug_context(context, format, ...) debug( \
		"[context %p] " format, \
		context, \
		##__VA_ARGS__)

#define debug_query(query, format, ...) debug( \
		"[query %p %s %s] " format, \
		query, \
		query->backend  && *query->backend ? (*query->backend)->settings[0] : "-", \
		netresolve_query_state_to_string(query->state), \
		##__VA_ARGS__)

enum netresolve_state {
	NETRESOLVE_STATE_NONE,
	NETRESOLVE_STATE_SETUP,
	NETRESOLVE_STATE_WAITING,
	NETRESOLVE_STATE_WAITING_MORE,
	NETRESOLVE_STATE_RESOLVED,
	NETRESOLVE_STATE_DONE,
	NETRESOLVE_STATE_ERROR,
	NETRESOLVE_STATE_FAILED
};

enum netresolve_request_type {
	NETRESOLVE_REQUEST_FORWARD,
	NETRESOLVE_REQUEST_REVERSE,
	NETRESOLVE_REQUEST_DNS,
	_NETRSOLVE_REQUEST_TYPES
};

enum netresolve_security {
	NETRESOLVE_SECURITY_INSECURE,
	NETRESOLVE_SECURITY_SECURE
};

struct netresolve_epoll {
	int fd;
	int count;
};

struct netresolve_backend {
	bool mandatory;
	char **settings;
	void *dl_handle;
	void (*setup[_NETRSOLVE_REQUEST_TYPES])(netresolve_query_t query, char **settings);
	void (*dispatch)(netresolve_query_t query, int fd, int revents);
	void (*cleanup)(netresolve_query_t query);
	void *data;
};

struct netresolve_path {
	struct {
		int family;
		union {
			char address[1024];
			struct in_addr address4;
			struct in6_addr address6;
		};
		int ifindex;
	} node;
	struct {
		int socktype;
		int protocol;
		int port;
	} service;
	int priority;
	int weight;
	int ttl;
	struct {
		enum netresolve_state state;
		int fd;
	} socket;
};

struct netresolve_query {
	struct netresolve_context *context;
	struct netresolve_source {
		netresolve_query_t query;
		int fd;
		void *handle;
		struct netresolve_source *previous, *next;
	} sources;
	netresolve_query_callback callback;
	void *user_data;
	enum netresolve_state state;
	int nfds;
	int delayed_fd;
	int timeout_fd;
	int partial_timeout_fd;
	struct netresolve_backend **backend;
	struct netresolve_request {
		enum netresolve_request_type type;
		/* Perform L3 address resolution using 'nodename' if not NULL. Use
		 * 'family' to chose between IPv4, IPv6 and mixed IPv4/IPv6
		 * resolution and additional flags to further tweak nodename name
		 * resolution.
		 */
		char *nodename;
		int family;
		/* Perform L4 port resolution using 'servname' if not NULL. Use
		 * 'socktype' and 'protocol' to limit the possible options and
		 * additional flags to further tweak servname name resolution.
		 */
		char *servname;
		int socktype;
		int protocol;
		int port;
		/* Advanced configuration */
		bool default_loopback;
		bool dns_srv_lookup;
		bool dns_search;
		int clamp_ttl;
		/* Reverse query */
		union {
			char address[1024];
			struct in_addr address4;
			struct in6_addr address6;
		};
		int ifindex;
		/* DNS data query */
		char *dns_name;
		int dns_class;
		int dns_type;
		/* Timeout configuration */
		int timeout;
		int partial_timeout;
	} request;
	struct netresolve_response {
		struct netresolve_path *paths;
		size_t pathcount;
		char *nodename;
		char *servname;
		struct {
			void *answer;
			size_t length;
		} dns;
		enum netresolve_security security;
	} response;

	struct netresolve_service_list *services;

	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
	} sa_buffer;
	char buffer[1024];
	struct netresolve_query *previous, *next;
};

struct netresolve_context {
	struct netresolve_query queries;
	struct netresolve_request request;
	struct netresolve_epoll epoll;
	int nfds;
	struct netresolve_backend **backends;
	struct {
		netresolve_watch_fd_callback_t watch_fd;
		netresolve_unwatch_fd_callback_t unwatch_fd;
		void *user_data;
		netresolve_free_user_data_callback_t free_user_data;
	} callbacks;
	struct netresolve_config {
		int force_family;
	} config;
};

/* Query */
netresolve_query_t netresolve_query(netresolve_t context, netresolve_query_callback callback, void *user_data,
		enum netresolve_option type, ...);
const char *netresolve_query_state_to_string(enum netresolve_state state);
void netresolve_query_set_state(netresolve_query_t query, enum netresolve_state state);
bool netresolve_query_dispatch(netresolve_query_t query, int fd, int events);

/* Request */
bool netresolve_request_set_options_from_va(struct netresolve_request *request, va_list ap);
bool netresolve_request_get_options_from_va(struct netresolve_request *request, va_list ap);

/* Event handling */
void netresolve_watch_fd(netresolve_query_t query, int fd, int events);
void netresolve_unwatch_fd(netresolve_query_t query, int fd);
int netresolve_add_timeout(netresolve_query_t query, time_t sec, long nsec);
int netresolve_add_timeout_ms(netresolve_query_t query, time_t msec);
void netresolve_remove_timeout(netresolve_query_t query, int fd);

/* Services */
struct netresolve_service_list;
typedef void (*netresolve_service_callback)(const char *name, int socktype, int protocol, int port, void *user_data);
struct netresolve_service_list *netresolve_service_list_new(const char *path);
void netresolve_service_list_free(struct netresolve_service_list *services);
void netresolve_service_list_query(struct netresolve_service_list **services,
		const char *name, int socktype, int protocol, int port,
		netresolve_service_callback callback, void *user_data);

/* Utilities */
int netresolve_family_from_string(const char *str);
int netresolve_socktype_from_string(const char *str);
int netresolve_protocol_from_string(const char *str);

/* String output */
const char *netresolve_get_request_string(netresolve_query_t query);
const char *netresolve_get_path_string(netresolve_query_t query, int i);
const char *netresolve_get_response_string(netresolve_query_t query);

/* Socket */
bool netresolve_connect_dispatch(netresolve_query_t query, int fd, int events);

/* Event loop for blocking mode */
bool netresolve_epoll_install(netresolve_t context,
		struct netresolve_epoll *loop,
		netresolve_free_user_data_callback_t free_loop);
void netresolve_epoll_wait(netresolve_t context);

#endif /* NETRESOLVE_PRIVATE_H */
