#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <netresolve.h>

static struct addrinfo *
generate_addrinfo_list(const netresolve_t resolver)
{
	size_t npaths = netresolve_get_path_count(resolver);
	const char *canonname = netresolve_get_canonical_name(resolver);
	struct addrinfo head = {0};
	struct addrinfo *ai = &head;
	int i;

	for (i = 0; i < npaths; i++) {
		int socktype, protocol;
		socklen_t salen;
		const struct sockaddr *sa = netresolve_get_path_sockaddr(resolver, i, &socktype, &protocol, &salen);

		ai = ai->ai_next = calloc(1, sizeof *ai + salen);
		if (!ai)
			goto fail;
		ai->ai_family = sa->sa_family;
		ai->ai_socktype = socktype;
		ai->ai_protocol = protocol;
		ai->ai_addrlen = salen;
		ai->ai_addr = (struct sockaddr *) (ai + 1);
		memcpy(ai->ai_addr, sa, salen);

		if (i == 0 && canonname)
			ai->ai_canonname = strdup(canonname);
	}

	return head.ai_next;
fail:
	freeaddrinfo(head.ai_next);
	return NULL;
}

int
getaddrinfo(const char *node, const char *service,
		const struct addrinfo *hints, struct addrinfo **res)
{
	netresolve_t resolver = netresolve_open();
	struct addrinfo default_hints = { 0 };
	int status;

	if (!hints)
		hints = &default_hints;

	if (hints->ai_flags & AI_PASSIVE)
		netresolve_unset_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);
	else
		netresolve_set_flag(resolver, NETRESOLVE_FLAG_DEFAULT_LOOPBACK);

	status = netresolve_resolve(resolver, node, service, hints->ai_family, hints->ai_socktype, hints->ai_protocol);

	if (status)
		return EAI_SYSTEM;

	*res = generate_addrinfo_list(resolver);
	return *res ? 0 : EAI_NODATA;
}
