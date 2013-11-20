/* Copyright (c) 2013 Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "netresolve-private.h"
#include "netresolve-string.h"

static int
family_from_string(const char *str)
{
	if (!strcmp(str, "ip4"))
		return AF_INET;
	if (!strcmp(str, "ip6"))
		return AF_INET6;
	if (!strcmp(str, "unix"))
		return AF_UNIX;
	/* "any" */
	return AF_UNSPEC;
}

static int
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

static int
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

netresolve_query_t
netresolve_query_argv(netresolve_t channel, char **argv)
{
	const char *node = NULL, *service = NULL;
	netresolve_query_t query;

	if (*argv && !strcmp(*argv, "-v")) {
		netresolve_set_log_level(NETRESOLVE_LOG_LEVEL_DEBUG);
		argv++;
	}

	if (*argv && !strcmp(*argv, "--srv")) {
		netresolve_set_dns_srv_lookup(channel, true);
		argv++;
	}

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
		netresolve_set_family(channel, family_from_string(*argv++));
	if (*argv)
		netresolve_set_socktype(channel, socktype_from_string(*argv++));
	if (*argv)
		netresolve_set_protocol(channel, protocol_from_string(*argv++));

	query = netresolve_query(channel, node, service);

	debug("%s", netresolve_get_request_string(query));

	return query;
}
