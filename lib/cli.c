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
#include <getopt.h>
#include <string.h>
#include <arpa/nameser.h>
#include <ldns/ldns.h>

#include "netresolve-private.h"

static int
count_argv(char **argv)
{
	int count = 0;

	while (*argv++)
		count++;

	return count;
}

netresolve_query_t
netresolve_query_argv(netresolve_t channel, char **argv)
{
	static const struct option longopts[] = {
		{ "help", 0, 0, 'h' },
		{ "verbose", 0, 0, 'v' },
		{ "node", 1, 0, 'n' },
		{ "host", 1, 0, 'n' },
		{ "service", 1, 0, 's' },
		{ "family", 1, 0, 'f' },
		{ "socktype", 1, 0, 't' },
		{ "protocol", 1, 0, 'p' },
		{ "backends", 1, 0, 'b' },
		{ "srv", 0, 0, 'S' },
		{ "address", 1, 0, 'a' },
		{ "port", 1, 0, 'P' },
		{ "class", 1, 0, 'C' },
		{ "type", 1, 0, 'T' },
		{ NULL, 0, 0, 0 }
	};
	static const char *opts = "hvn::s:f:t:p:b:Sa:P:";
	int opt, idx = 0;
	char *node = NULL, *service = NULL;
	char *address_str = NULL, *port_str = NULL;
	int cls = ns_c_in, type = 0;
	netresolve_query_t query;

	while ((opt = getopt_long(count_argv(argv), argv, opts, longopts, &idx)) != -1) {
		switch (opt) {
		case 'h':
			fprintf(stderr,
					"-h,--help -- help\n"
					"-v,--verbose -- show more verbose output\n"
					"-n,--node <nodename> -- node name\n"
					"-s,--service <servname> -- service name\n"
					"-f,--family any|ip4|ip6 -- family name\n"
					"-t,--socktype any|stream|dgram|seqpacket -- socket type\n"
					"-p,--protocol any|tcp|udp|sctp -- transport protocol\n"
					"-b,--backends <backends> -- comma-separated list of backends\n"
					"-S,--srv -- resolve DNS SRV record\n"
					"-a,--address -- IPv4/IPv6 address (reverse query)\n"
					"-P,--port -- TCP/UDP port\n"
					"-C,--class -- DNS record class\n"
					"-T,--type -- DNS record type\n");
			exit(EXIT_SUCCESS);
		case 'n':
			node = optarg;
			break;
		case 's':
			service = optarg;
			break;
		case 'f':
			netresolve_set_family(channel, netresolve_family_from_string(optarg));
			break;
		case 't':
			netresolve_set_socktype(channel, netresolve_socktype_from_string(optarg));
			break;
		case 'p':
			netresolve_set_protocol(channel, netresolve_protocol_from_string(optarg));
			break;
		case 'v':
			netresolve_set_log_level(NETRESOLVE_LOG_LEVEL_DEBUG);
			break;
		case 'b':
			netresolve_set_backend_string(channel, optarg);
			break;
		case 'S':
			netresolve_set_dns_srv_lookup(channel, true);
			break;
		case 'a':
			address_str = optarg;
			break;
		case 'P':
			port_str = optarg;
			break;
		case 'C':
			cls = ldns_get_rr_class_by_name(optarg);
			break;
		case 'T':
			type = ldns_get_rr_type_by_name(optarg);
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}

	if (argv[optind])
		abort();

	if (type)
		query = netresolve_query_dns(channel, node, cls, type);
	else if (address_str || port_str) {
		Address address;
		int family, ifindex;

		netresolve_backend_parse_address(address_str, &address, &family, &ifindex);
		query = netresolve_query_reverse(channel, family, &address, ifindex, port_str ? strtol(port_str, NULL, 10) : 0);
	} else
		query = netresolve_query(channel, node, service);

	debug("%s", netresolve_get_request_string(query));

	return query;
}
