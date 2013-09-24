#include <netresolve.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int
family_from_string(const char *str)
{
	if (!strcmp(str, "ip4"))
		return AF_INET;
	if (!strcmp(str, "ip6"))
		return AF_INET6;
	/* "any" */
	return AF_UNSPEC;
}

int
socktype_from_string(const char *str)
{
	if (!strcmp(str, "stream"))
		return SOCK_STREAM;
	if (!strcmp(str, "dgram"))
		return SOCK_DGRAM;
	if (!strcmp(str, "seqpacket"))
		return SOCK_SEQPACKET;
	return 0;
}

int
protocol_from_string(const char *str)
{
	if (!strcmp(str, "tcp"))
		return IPPROTO_TCP;
	if (!strcmp(str, "udp"))
		return IPPROTO_UDP;
	if (!strcmp(str, "sctp"))
		return IPPROTO_SCTP;
	return 0;
}

int
netresolve_resolve_argv(netresolve_t resolver, char **argv)
{
	const char *node = NULL, *service = NULL;
	int family = 0, socktype = 0, protocol = 0;

	if (*argv) {
		node = *argv++;
		if (node[0] == '-' && !node[1])
			node = NULL;
	}
	if (*argv) {
		service = *argv++;
		if (service[0] == '-' && !service[1])
			service = NULL;
	} if (*argv)
		family = family_from_string(*argv++);
	if (*argv)
		socktype = socktype_from_string(*argv++);
	if (*argv)
		protocol = protocol_from_string(*argv++);

	return netresolve_resolve(resolver, node, service, family, socktype, protocol);
}
