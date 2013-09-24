#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include <netresolve-backend.h>

static void
add_addresses(netresolve_backend_t response, const struct addrinfo *first)
{
	const struct addrinfo *ai;

	for (ai = first; ai; ai = ai->ai_next) {
		union {
			struct in_addr address4;
			struct in6_addr address6;
		} address = {{0}};
		int ifindex = 0;
		int port;

		if (!ai->ai_family)
			continue;

		switch (ai->ai_family) {
		case AF_INET:
			memcpy(&address.address4, &((struct sockaddr_in *) ai->ai_addr)->sin_addr, sizeof address.address4);
			port = ntohs(((struct sockaddr_in *) ai->ai_addr)->sin_port);
			break;
		case AF_INET6:
			memcpy(&address.address6, &((struct sockaddr_in6 *) ai->ai_addr)->sin6_addr, sizeof address.address6);
			if (IN6_IS_ADDR_LINKLOCAL(&address.address6))
				ifindex = ((struct sockaddr_in6 *) ai->ai_addr)->sin6_scope_id;
			port = ntohs(((struct sockaddr_in6 *) ai->ai_addr)->sin6_port);
			break;
		}

		netresolve_backend_add_path(response,
				ai->ai_family, &address, ifindex,
				ai->ai_socktype, ai->ai_protocol, port);
	}

}

void
start(netresolve_backend_t resolver, char **settings)
{
	const char *node = netresolve_backend_get_node(resolver);
	const char *service = netresolve_backend_get_service(resolver);
	struct addrinfo hints = {
		.ai_family = netresolve_backend_get_family(resolver),
		.ai_socktype = netresolve_backend_get_socktype(resolver),
		.ai_protocol = netresolve_backend_get_protocol(resolver),
		.ai_flags = 0,
	};
	struct addrinfo *result;
	int status;

	if (!netresolve_backend_get_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK))
		hints.ai_flags |= AI_PASSIVE;

	status = getaddrinfo(node, service, &hints, &result);

	switch (status) {
	case 0:
		add_addresses(resolver, result);
		freeaddrinfo(result);
		/* fall through */
	case EAI_NONAME:
	//case EAI_NODATA:
		netresolve_backend_finished(resolver);
		break;
	default:
		netresolve_backend_failed(resolver);
	}
}
