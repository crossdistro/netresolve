#include <stdlib.h>
#include <arpa/inet.h>

#include <netresolve-backend.h>

static struct in_addr inaddr_any = { 0 };

void
start(netresolve_backend_t resolver, char **settings)
{
	const char *node = netresolve_backend_get_node(resolver);
	bool loopback = netresolve_backend_get_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);

	/* Fail for non-NULL node name and when defaulting to loopback is requested. */
	if (loopback || (node && *node)) {
		netresolve_backend_failed(resolver);
		return;
	}

	netresolve_backend_add_address(resolver, AF_INET, &inaddr_any, 0);
	netresolve_backend_add_address(resolver, AF_INET6, &in6addr_any, 0);
	netresolve_backend_finished(resolver);
}
