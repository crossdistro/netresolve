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
#include <string.h>
#include <assert.h>
#include <netdb.h>

int
main(int argc, char **argv)
{
#ifdef GETHOSTBYNAME2
	const char *node = "1:2:3:4:5:6:7:8";
#else
	const char *node = "1.2.3.4";
#endif
	struct hostent *result = NULL;

#ifdef REENTRANT
	struct hostent he;
	size_t buflen = 1024;
	char buffer[buflen];
	int my_errno, my_h_errno;
#ifdef GETHOSTBYNAME2
	my_errno = gethostbyname2_r(node, AF_INET6, &he, buffer, buflen, &result, &my_h_errno);
#else
	my_errno = gethostbyname_r(node, &he, buffer, buflen, &result, &my_h_errno);
#endif
	assert(!my_errno);
	assert(result == &he);
#else
#ifdef GETHOSTBYNAME2
	result = gethostbyname2(node, AF_INET6);
#else
	result = gethostbyname(node);
#endif
#endif
	assert(result && result->h_addr_list[0]);

	struct {
		struct hostent he;
		char *ha[1];
		char *al[2];
#ifdef GETHOSTBYNAME2
		struct in6_addr ia;
#else
		struct in_addr ia;
#endif
	} expected = {
		.he = {
			.h_name = result->h_name,
			.h_aliases = result->h_aliases,
#ifdef GETHOSTBYNAME2
			.h_addrtype = AF_INET6,
#else
			.h_addrtype = AF_INET,
#endif
			.h_length = sizeof expected.ia,
			.h_addr_list = result->h_addr_list,
		},
		.ha = { NULL },
		.al = { *result->h_addr_list, NULL },
#ifdef GETHOSTBYNAME2
		.ia = { .s6_addr = { 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8} }
#else
		.ia = { .s_addr = htonl(0x01020304) }
#endif
	};
	assert(!memcmp(result, &expected.he, sizeof expected.he));
	assert(result->h_name && !strcmp(result->h_name, node));
	assert(!memcmp(result->h_aliases, &expected.ha, sizeof expected.ha));
	assert(!memcmp(result->h_addr_list, &expected.al, sizeof expected.al));
	assert(!memcmp(result->h_addr_list[0], &expected.ia, sizeof expected.ia));

	return EXIT_SUCCESS;
}
