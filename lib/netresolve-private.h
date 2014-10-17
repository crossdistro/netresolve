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
#include <netresolve.h>
#include <netresolve-backend.h>
#include <netresolve-cli.h>
#include <nss.h>
#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <assert.h>

enum netresolve_state {
	NETRESOLVE_STATE_INIT,
	NETRESOLVE_STATE_WAITING,
	NETRESOLVE_STATE_FINISHED,
	NETRESOLVE_STATE_FAILED
};

enum netresolve_request_type {
	NETRESOLVE_REQUEST_FORWARD,
	NETRESOLVE_REQUEST_REVERSE,
	NETRESOLVE_REQUEST_DNS,
	_NETRSOLVE_REQUEST_TYPES
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

struct netresolve_channel {
	int epoll_fd;
	int epoll_count;
	struct netresolve_backend **backends;
	struct {
		netresolve_callback_t on_success;
		netresolve_callback_t on_failure;
		void *user_data;
		netresolve_fd_callback_t watch_fd;
		void *user_data_fd;
		netresolve_socket_callback_t on_bind;
		netresolve_socket_callback_t on_connect;
		void *user_data_sock;
	} callbacks;
	struct netresolve_request {
		void *user_data;
		enum netresolve_request_type type;
		/* Perform L3 address resolution using 'nodename' if not NULL. Use
		 * 'family' to chose between IPv4, IPv6 and mixed IPv4/IPv6
		 * resolution and additional flags to further tweak nodename name
		 * resolution.
		 */
		const char *nodename;
		int family;
		/* Perform L4 port resolution using 'servname' if not NULL. Use
		 * 'socktype' and 'protocol' to limit the possible options and
		 * additional flags to further tweak servname name resolution.
		 */
		const char *servname;
		int socktype;
		int protocol;
		/* Advanced configuration */
		bool default_loopback;
		bool dns_srv_lookup;
		/* Reverse query */
		union {
			char address[1024];
			struct in_addr address4;
			struct in6_addr address6;
		};
		/* DNS data query */
		char *dns_name;
		int dns_class;
		int dns_type;
	} request;
	struct netresolve_config {
		int force_family;
	} config;
	/* A list of queries */
	netresolve_query_t *queries;
	size_t nqueries;
};

struct netresolve_query {
	struct netresolve_channel *channel;
	enum netresolve_state state;
	int first_connect_timeout;
	struct netresolve_backend **backend;
	struct netresolve_request request;
	struct netresolve_response {
		struct netresolve_path *paths;
		size_t pathcount;
		char *canonname;
		struct {
			void *answer;
			size_t length;
		} dns;
	} response;

	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
	} sa_buffer;
	char buffer[1024];
};

void netresolve_query_set_state(netresolve_query_t query, enum netresolve_state state);

void netresolve_query_start(netresolve_query_t channel);
bool netresolve_epoll(netresolve_t channel, int timeout);
void netresolve_watch_fd(netresolve_t channel, int fd, int events);
int netresolve_add_timeout(netresolve_t channel, time_t sec, long nsec);
void netresolve_remove_timeout(netresolve_t channel, int fd);

void netresolve_query_bind(netresolve_query_t query, size_t idx);
void netresolve_query_connect(netresolve_query_t query, size_t idx);

void netresolve_connect_start(netresolve_query_t query);
bool netresolve_connect_dispatch(netresolve_query_t query, int fd, int events);
void netresolve_connect_cleanup(netresolve_query_t query);

void netresolve_get_service_info(void (*callback)(int, int, int, void *), void *user_data,
		const char *request_service, int socktype, int protocol);

int netresolve_family_from_string(const char *str);
int netresolve_socktype_from_string(const char *str);
int netresolve_protocol_from_string(const char *str);

const char * netresolve_get_request_string(netresolve_query_t query);
const char * netresolve_get_path_string(netresolve_query_t query, int i);
const char * netresolve_get_response_string(netresolve_query_t query);
