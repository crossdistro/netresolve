#include <netresolve-backend.h>

void
start(netresolve_backend_t resolver, char **settings)
{
	const char *request_node = netresolve_backend_get_node(resolver);
	Address address;
	int family;
	int ifindex;

	if (!netresolve_backend_parse_address(request_node, &address, &family, &ifindex)) {
		netresolve_backend_failed(resolver);
		return;
	}

	netresolve_backend_add_address(resolver, family, &address, ifindex);
	netresolve_backend_finished(resolver);
}
