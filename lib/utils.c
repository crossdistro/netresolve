#include <unistd.h>
#include <errno.h>

#include <netresolve.h>
#include <netresolve-utils.h>

static int
netresolve_bind(netresolve_t resolver, int flags)
{
	size_t npaths = netresolve_get_path_count(resolver);
	size_t i;

	for (i = 0; i < npaths; i++) {
		int socktype;
		int protocol;
		const struct sockaddr *sa;
		socklen_t salen;
		int sock;

		sa = netresolve_get_path_sockaddr(resolver, i, &socktype, &protocol, &salen);
		if (!sa)
			continue;
		sock = socket(sa->sa_family, socktype | flags, protocol);
		if (sock == -1)
			continue;
		if (bind(sock, sa, salen) == -1) {
			close(sock);
			continue;
		}

		return sock;
	}

	return -1;
}

static int
netresolve_connect(netresolve_t resolver, int flags)
{
	size_t npaths = netresolve_get_path_count(resolver);
	size_t i;

	for (i = 0; i < npaths; i++) {
		int socktype;
		int protocol;
		const struct sockaddr *sa;
		socklen_t salen;
		int sock;

		sa = netresolve_get_path_sockaddr(resolver, i, &socktype, &protocol, &salen);
		if (!sa)
			continue;
		sock = socket(sa->sa_family, socktype | flags, protocol);
		if (sock == -1)
			continue;
		if (connect(sock, sa, salen) == -1) {
			close(sock);
			continue;
		}

		return sock;
	}

	return -1;
}

int
netresolve_utils_bind(const char *node, const char *service, int family, int socktype, int protocol)
{
	netresolve_t resolver = netresolve_open();
	int flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC);
	int status;

	if (!resolver)
		return -1;

	netresolve_unset_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);
	socktype &= ~flags;

	status = netresolve_resolve(resolver, node, service, family, socktype, protocol);
	if (status) {
		errno = status;
		return -1;
	}

	return netresolve_bind(resolver, flags);
}

int
netresolve_utils_connect(const char *node, const char *service, int family, int socktype, int protocol)
{
	netresolve_t resolver = netresolve_open();
	int flags = socktype & (SOCK_NONBLOCK | SOCK_CLOEXEC);
	int status;

	if (!resolver)
		return -1;

	netresolve_set_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);
	socktype &= ~flags;

	status = netresolve_resolve(resolver, node, service, family, socktype, protocol);
	if (status) {
		errno = status;
		return -1;
	}

	return netresolve_connect(resolver, flags);
}
