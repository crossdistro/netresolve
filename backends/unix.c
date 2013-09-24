#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>

#include <netresolve-backend.h>

void
start(netresolve_backend_t resolver, char **settings)
{
	const char *node = netresolve_backend_get_node(resolver);
	int family = netresolve_backend_get_family(resolver);
	int socktype = netresolve_backend_get_socktype(resolver);

	if ((family != AF_UNIX && family != AF_UNSPEC) || !node || *node != '/'){
		netresolve_backend_failed(resolver);
		return;
	}

	netresolve_backend_add_path(resolver, AF_UNIX, node, 0, socktype, 0, 0);
	netresolve_backend_finished(resolver);
}
