/* Copyright (c) 2013+ Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
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

static int
print_usage()
{
	return fprintf(stderr,
			"-h,--help -- help\n"
			"-n,--node <nodename> -- node name\n"
			"-4,--ipv4 -- IPv4 only query\n"
			"-6,--ipv6 -- IPv6 only query\n");
}

int
main(int argc, char **argv)
{
	static const struct option longopts[] = {
		{ "help", 0, 0, 'h' },
		{ "verbose", 0, 0, 'v' },
		{ "node", 1, 0, 'n' },
		{ "host", 1, 0, 'n' },
		{ "ipv4", 0, 0, '4' },
		{ "ipv6", 0, 0, '6' },
		{ NULL, 0, 0, 0 }
	};
	static const char *opts = "hvn:46";
	int opt, idx = 0;
	char *nodename = NULL;
	int family = 0;

	while ((opt = getopt_long(argc, argv, opts, longopts, &idx)) != -1) {
		switch (opt) {
		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
		case 'n':
			nodename = optarg;
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
		nodename = argv[optind++];
	if (argv[optind]) {
		fprintf(stderr, "Too many arguments.");
		exit(1);
	}

	if (!nodename) {
		print_usage();
		exit(1);
	}

	printf("query:\n");
	printf("  api = %s\n", family ? "gethostbyname2" : "gethostbyname");
	if (family)
		printf(" family = %d\n", family);
	printf("  nodename = %s\n", nodename);

	struct hostent *result = family ? gethostbyname2(nodename, family) : gethostbyname(nodename);

	if (!result) {
		printf("errno = %d\n", errno);
		printf("h_errno = %d\n", h_errno);
		exit(1);
	}

	printf("result:\n");
	print_hostent(result);
}
