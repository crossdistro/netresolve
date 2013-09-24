#ifndef NETRESOLVE_UTILS_H
#define NETRESOLVE_UTILS_H

#include <netinet/in.h>

int netresolve_utils_connect(const char *node, const char *service, int socktype, int protocol, int port);
int netresolve_utils_bind(const char *node, const char *service, int socktype, int protocol, int port);

#endif /* NETRESOLVE_UTILS_H */
