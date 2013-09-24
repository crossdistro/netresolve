#ifndef NETRESOLVE_CLI_H
#define NETRESOLVE_CLI_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct netresolve_resolver *netresolve_t;

int netresolve_resolve_argv(netresolve_t resolver, char **argv);

#endif /* NETRESOLVE_CLI_H */
