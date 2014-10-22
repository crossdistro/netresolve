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
#include <netdb.h>

/* struct addrinfo {
 *     int ai_flags;
 *     int ai_family;
 *     int ai_socktype;
 *     int ai_protocol;
 *     socklen_t ai_addrlen;
 *     struct sockaddr *ai_addr;
 *     char *ai_canonname;
 *     struct addrinfo *ai_next;
 * };
 *
 * struct sockaddr_in {
 *     sa_family_t sin_family;
 *     in_port_t sin_port;
 *     struct in_addr sin_addr;
 * };
 *
 * struct sockaddr_in6 {
 *     sa_family_t sin6_family;
 *     in_port_t sin6_port;
 *     uint32_t sin6_flowinfo;
 *     struct in6_addr sin6_addr;
 *     uint32_t sin6_scope_id;
 * };
 */

void
print_addrinfo(struct addrinfo *item)
{
	if (item->ai_flags)
		printf("  flags = %d\n", item->ai_flags);
	if (item->ai_family)
		printf("  family = %d\n", item->ai_family);
	if (item->ai_socktype)
		printf("  socktype = %d\n", item->ai_socktype);
	if (item->ai_protocol)
		printf("  protocol = %d\n", item->ai_protocol);
	if (item->ai_addrlen)
		printf("  addrlen = %d\n", item->ai_addrlen);
	if (item->ai_addr) {
		printf("  address:\n");
		printf("    family = %d\n", item->ai_addr->sa_family);
		switch (item->ai_addr->sa_family) {
		case AF_INET:
			{
				struct sockaddr_in *sa4 = (void *) item->ai_addr;

				printf("    port = %d\n", ntohs(sa4->sin_port));
				printf("    address = %d\n", ntohl(sa4->sin_addr.s_addr));
			}
			break;
		case AF_INET6:
			{
				struct sockaddr_in6 *sa6 = (void *) item->ai_addr;

				printf("    port = %d\n", ntohs(sa6->sin6_port));
				printf("    flowinfo = %08x\n", ntohs(sa6->sin6_port));
				printf("    address = ");
				for (int i = 0; i < 16; i++)
					printf("%02x", sa6->sin6_addr.s6_addr[i]);
				printf("\n");
				printf("    scope_id = %d\n", sa6->sin6_scope_id);
			}
			break;
		default:
			break;
		}
	}
	if (item->ai_canonname)
		printf("  canonname = %s\n", item->ai_canonname);
}

int
main(int argc, char **argv)
{
	static const struct option longopts[] = {
		{ "help", 0, 0, 'h' },
		{ "verbose", 0, 0, 'v' },
		{ "node", 1, 0, 'n' },
		{ "host", 1, 0, 'n' },
		{ "service", 0, 0, 's' },
		{ "ipv4", 0, 0, '4' },
		{ "ipv6", 0, 0, '6' },
		{ "raw", 0, 0, 'R' },
		{ "stream", 0, 0, 'S' },
		{ "dgram", 0, 0, 'D' },
		{ "datagram", 0, 0, 'D' },
		{ "tcp", 0, 0, 'T' },
		{ "udp", 0, 0, 'U' },
		{ "passive", 0, 0, 'p' },
		{ "addrconfig", 0, 0, 'a' },
		{ "canonname", 0, 0, 'c' },
		{ NULL, 0, 0, 0 }
	};
	static const char *opts = "hvn:s:46RSDTUpac";
	int opt, idx = 0;
	char *nodename = NULL, *servname = NULL;
	struct addrinfo hints = { 0 };
	struct addrinfo *result;

	while ((opt = getopt_long(argc, argv, opts, longopts, &idx)) != -1) {
		switch (opt) {
		case 'h':
			fprintf(stderr,
					"-h,--help -- help\n"
					"-n,--node <nodename> -- node name\n"
					"-s,--service <servname> -- service name\n"
					"-4,--ipv4 -- IPv4 only query\n"
					"-6,--ipv6 -- IPv6 only query\n"
					"--raw -- raw socket type (SOCK_RAW)\n"
					"--stream -- stream socket type (SOCK_STREAM)\n"
					"--dgram -- datagram socket type (SOCK_DGRAM)\n"
					"--tcp -- TCP protocol (IPPROTO_TCP)\n"
					"--udp -- UDP protocol (IPPROTO_UDP)\n"
					"--passive -- AI_PASSIVE\n"
					"--addrconfig -- AI_ADDRCONFIG\n"
					"--canonname -- AI_CANONNAME\n");
			exit(EXIT_SUCCESS);
		case 'n':
			nodename = optarg;
			break;
		case 's':
			servname = optarg;
			break;
		case '4':
			hints.ai_family = AF_INET;
			break;
		case '6':
			hints.ai_family = AF_INET6;
			break;
		case 'R':
			hints.ai_socktype = SOCK_RAW;
			break;
		case 'S':
			hints.ai_socktype = SOCK_STREAM;
			break;
		case 'D':
			hints.ai_socktype = SOCK_DGRAM;
			break;
		case 'T':
			hints.ai_protocol = IPPROTO_TCP;
			break;
		case 'U':
			hints.ai_protocol = IPPROTO_UDP;
			break;
		case 'p':
			hints.ai_flags |= AI_PASSIVE;
			break;
		case 'a':
			hints.ai_flags |= AI_ADDRCONFIG;
			break;
		case 'c':
			hints.ai_flags |= AI_CANONNAME;
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}

	if (argv[optind])
		nodename = argv[optind++];
	if (argv[optind])
		servname = argv[optind++];
	if (argv[optind]) {
		fprintf(stderr, "Too many arguments.");
		exit(1);
	}

	printf("query:\n");
	printf("  nodename = %s\n", nodename);
	printf("  servname = %s\n", servname);
	print_addrinfo(&hints);

	int status = getaddrinfo(nodename, servname, &hints, &result);

	printf("status = %d\n", status);
	if (status)
		exit(1);
	int count = 0;
	for (struct addrinfo *item = result; item; item = item->ai_next) {
		printf("#%d:\n", count++);
		print_addrinfo(item);
	}

	freeaddrinfo(result);
}
