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
#include <netresolve.h>
#include <netresolve-cli.h>
#include <ldns/ldns.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>


static char *
get_dns_string(netresolve_query_t query)
{
	const uint8_t *answer;
	size_t length;

	if (!(answer = netresolve_query_get_dns_answer(query, &length)))
		return NULL;

	ldns_pkt *pkt;

	int status = ldns_wire2pkt(&pkt, answer, length);

	if (status) {
		fprintf(stderr, "ldns: %s", ldns_get_errorstr_by_id(status));
		return NULL;
	}
	
	char *result = ldns_pkt2str(pkt);

	ldns_pkt_free(pkt);
	return result;
}

int
main(int argc, char **argv)
{
	netresolve_t channel;
	netresolve_query_t query;

	channel = netresolve_open();
	if (!channel) {
		fprintf(stderr, "netresolve: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	query = netresolve_query_argv(channel, argv);
	if (!query) {
		fprintf(stderr, "netresolve: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	const char *response_string = netresolve_get_response_string(query);
	char *dns_string = get_dns_string(query);

	if (response_string)
		printf("%s", response_string);
	if (dns_string) {
		printf("%s", dns_string);
		free(dns_string);
	}

	netresolve_close(channel);
	return EXIT_SUCCESS;
}
