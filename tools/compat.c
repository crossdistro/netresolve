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
#include <arpa/inet.h>
#include <net/if.h>

#include "compat.h"

/*
 * struct hostent {
 *     char *h_name;
 *     char **h_aliases;
 *     int h_addrtype;
 *     int h_length;
 *     char **h_addr_list;
 * }
 * #define h_addr h_addr_list[0]
 */

void
print_hostent(struct hostent *item)
{
	if (item->h_name)
		printf("  name = %s\n", item->h_name);
	if (item->h_aliases) {
		int count = 0;
		printf("  aliases:\n");
		for (char **alias = item->h_aliases; *alias; alias++)
			printf("    #%d = %s\n", count++, *alias);
	}
	if (item->h_addrtype)
		printf("  addrtype = %d\n", item->h_addrtype);
	if (item->h_length)
		printf("  length = %d\n", item->h_length);
	if (item->h_addr) {
		printf("  address = ");
		for (int i = 0; i < item->h_length; i++)
			printf("%02x", item->h_addr[i]);
		printf("\n");
	}
	if (item->h_addr_list) {
		int count = 0;
		printf("  addresses:\n");
		for (char **address = item->h_addr_list; *address; address++) {
			printf("    #%d = ", count++);
			for (int i = 0; i < item->h_length; i++)
				printf("%02x", (*address)[i]);
			printf("\n");
		}
	}
}

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
				printf("    address = 0x%x\n", ntohl(sa4->sin_addr.s_addr));
			}
			break;
		case AF_INET6:
			{
				struct sockaddr_in6 *sa6 = (void *) item->ai_addr;

				printf("    port = %d\n", ntohs(sa6->sin6_port));
				printf("    flowinfo = 0x%08x\n", ntohs(sa6->sin6_flowinfo));
				printf("    address = 0x");
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
		printf("  nodename = %s\n", item->ai_canonname);
}

bool
parse_address(const char *string_orig, void *address, int *family, int *ifindex)
{
	char *string;
	char *ifname;

	if (!string_orig)
		return false;

	string = strdupa(string_orig);

	ifname = strchr(string, '%');
	if (ifname)
		*ifname++ = '\0';
	if (!ifname)
		ifname = "";
	if (ifindex) {
		*ifindex = if_nametoindex(ifname);
		if (!*ifindex) {
			char *endptr = NULL;

			*ifindex = strtol(ifname, &endptr, 10);
			if (*endptr)
				return false;
		}
	}

	if (inet_pton(AF_INET, string, address) == 1) {
		*family = AF_INET;
		return true;
	}
	if (inet_pton(AF_INET6, string, address) == 1) {
		*family = AF_INET6;
		return true;
	}

	return false;
}
