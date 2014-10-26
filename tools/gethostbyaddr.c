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
#include "compat.h"

int
main(int argc, char **argv)
{
	static const struct option longopts[] = {
		{ "help", 0, 0, 'h' },
		{ "verbose", 0, 0, 'v' },
		{ "address", 1, 0, 'a' },
		{ NULL, 0, 0, 0 }
	};
	static const char *opts = "hva:46";
	int opt, idx = 0;
	char *address_str = NULL;
	char address[16];
	int family, ifindex;


	while ((opt = getopt_long(argc, argv, opts, longopts, &idx)) != -1) {
		switch (opt) {
		case 'h':
			fprintf(stderr,
					"-h,--help -- help\n"
					"-a,--address <address> -- node name\n");
			exit(EXIT_SUCCESS);
		case '1':
			address_str = optarg;
			break;
		case '4':
			family = AF_INET;
			break;
		case '6':
			family = AF_INET6;
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}

	if (argv[optind])
		address_str = argv[optind++];
	if (argv[optind]) {
		fprintf(stderr, "Too many arguments.");
		exit(1);
	}

	printf("query:\n");
	printf("  address = %s\n", address_str);

	if (!address_str) {
		fprintf(stderr, "Cannot query an empty address.\n");
		exit(1);
	}

	parse_address(address_str, &address, &family, &ifindex);

	if (ifindex) {
		fprintf(stderr, "No ifindex/scope_id support in `gethostbyaddr()`.");
		exit(1);
	}

	struct hostent *result = gethostbyaddr(&address, family == AF_INET ? 4 : 16, family);

	if (!result) {
		printf("errno = %d\n", errno);
		printf("h_errno = %d\n", h_errno);
		exit(1);
	}

	printf("result:\n");
	print_hostent(result);
}
