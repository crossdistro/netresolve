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
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <resolv.h>

#ifdef USE_LDNS
#include <ldns/ldns.h>
#endif

#include "compat.h"

#define SIZE 8192

static int
print_usage(FILE *out)
{
	return fprintf(out,
			"Resolve DNS query\n"
			"-h,--help -- help\n"
			"-s,--search <servname> -- service name\n"
			"-n,--dname <name> -- domain name\n"
			"-c,--class <class> -- class\n"
			"-t,--type <type> -- type\n");

}

int
main(int argc, char **argv)
{
	static const struct option longopts[] = {
		{ "help", 0, 0, 'h' },
		{ "verbose", 0, 0, 'v' },
		{ "search", 0, 0, 's' },
		{ "dname", 1, 0, 'n' },
		{ "class", 1, 0, 'c' },
		{ "type", 1, 0, 't' },
		{ NULL, 0, 0, 0 }
	};
	static const char *opts = "hvsn:c:t:";
	int opt, idx = 0;
	bool search = false;
	const char *dname = NULL;
	int class = ns_c_in;
	int type = ns_t_a;
	uint8_t answer[SIZE];
	int length = 0;

	while ((opt = getopt_long(argc, argv, opts, longopts, &idx)) != -1) {
		switch (opt) {
		case 'h':
			print_usage(stderr);
			exit(EXIT_SUCCESS);
		case 's':
			search = true;
			break;
		case 'n':
			dname = optarg;
			break;
		case 'c':
			class = strtoll(optarg, NULL, 10);
			break;
		case 't':
			type = strtoll(optarg, NULL, 10);
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}

	if (argv[optind]) {
		fprintf(stderr, "Too many arguments.\n");
		exit(1);
	}

	if (!dname) {
		fprintf(stderr, "Cannot query NULL dname.\n");
		print_usage(stderr);
		exit(EXIT_FAILURE);
	}

	printf("query:\n");
	printf("  search = %s\n", search ? "yes" : "no");
	printf("  dname = %s\n", dname);
	printf("  class = %d\n", class);
	printf("  type = %d\n", type);

	length = (search ? res_search : res_query)(dname, class, type, answer, sizeof answer);

	printf("result:\n");
	printf("  length = %d\n", length);

#ifdef USE_LDNS
	ldns_pkt *pkt;

	if (length > 0 && !ldns_wire2pkt(&pkt, answer, length)) {
		printf("  answer:\n%s\n", ldns_pkt2str(pkt));

		ldns_pkt_free(pkt);
	}
#endif

	exit(EXIT_SUCCESS);
}
