#ifndef NETRESOLVE_BACKEND_H
#define NETRESOLVE_BACKEND_H

#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#include <netresolve-common.h>

typedef struct netresolve_resolver *netresolve_backend_t;

/* Input */
const char *netresolve_backend_get_node(netresolve_backend_t resolver);
const char *netresolve_backend_get_service(netresolve_backend_t resolver);
int netresolve_backend_get_family(netresolve_backend_t resolver);
int netresolve_backend_get_protocol(netresolve_backend_t resolver);
int netresolve_backend_get_socktype(netresolve_backend_t resolver);
bool netresolve_backend_get_flag(netresolve_backend_t resolver, netresolve_flag_t flag);

/* Output */
void netresolve_backend_add_path(netresolve_backend_t resolver,
		int family, const void *address, int ifindex,
		int socktype, int protocol, int port);
void netresolve_backend_add_address(netresolve_backend_t resolver,
		int family, const void *address, int ifindex);
void netresolve_backend_set_canonical_name(netresolve_backend_t resolver, const char *canonical_name);

/* Tools */
void *netresolve_backend_new_data(netresolve_backend_t resolver, size_t size);
void *netresolve_backend_get_data(netresolve_backend_t resolver);
void netresolve_backend_watch_fd(netresolve_backend_t resolver, int fd, int events);
int netresolve_backend_watch_timeout(netresolve_backend_t resolver, time_t sec, long nsec);
void netresolve_backend_drop_timeout(netresolve_backend_t resolver, int fd);
void netresolve_backend_finished(netresolve_backend_t resolver);
void netresolve_backend_failed(netresolve_backend_t resolver);

/* Logging */
#define error(...) netresolve_backend_log(resolver, 1, __VA_ARGS__)
#define debug(...) netresolve_backend_log(resolver, 2, __VA_ARGS__)
void netresolve_backend_log(netresolve_backend_t resolver, int level, const char *fmt, ...);

/* Convenience */
typedef union { struct in_addr address4; struct in6_addr address6; } Address;
bool netresolve_backend_parse_address(const char *string_orig, Address *address, int *family, int *ifindex);
void netresolve_backend_apply_hostent(netresolve_backend_t resolver, const struct hostent *he, bool canonname);

/* Backend function prototypes */
void start(netresolve_backend_t resolver, char **settings);
void dispatch(netresolve_backend_t resolver, int fd, int revents);
void cleanup(netresolve_backend_t resolver);

#endif /* NETRESOLVE_BACKEND_H */
