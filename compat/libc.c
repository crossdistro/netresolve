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
#include <netresolve-compat.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <resolv.h>

/* getaddrinfo:
 *
 * Translate `nodename` and `servname` using `hints` into a linked list of
 * address records including both L3 and L4 information. Canonical name,
 * if requested, is present in the first address record.
 *
 * The caller is responsible for freeing the data using `freeaddrinfo()`. The
 * function is reentrant, threat safe and doesn't leak memory.
 *
 * Defined in POSIX.1-2008.
 */
int
getaddrinfo(const char *nodename, const char *servname,
		const struct addrinfo *hints, struct addrinfo **result)
{
	netresolve_t channel;
	netresolve_query_t query;
	int status = EAI_SYSTEM;

	if (!(channel = netresolve_open()))
		return status;

	if ((query = netresolve_query_getaddrinfo(channel, nodename, servname, hints)))
		status = netresolve_query_getaddrinfo_done(query, result, NULL);

	netresolve_close(channel);
	return status;
}

/* freeaddrinfo:
 *
 * Free data returned by `getaddrinfo()`.
 *
 * Defined in POSIX.1-2008.
 */
void
freeaddrinfo(struct addrinfo *result)
{
	netresolve_freeaddrinfo(result);
}

/* getnameinfo:
 *
 * Translate the `sa` into `host` and `serv`. The caller supplies buffers to
 * store the result.
 *
 * Defined in POSIX.1-2008.
 */
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
		char *host, socklen_t hostlen,
		char *serv, socklen_t servlen,
		int flags)
{
	netresolve_t channel;
	netresolve_query_t query;
	int status = EAI_SYSTEM;
	char *myhost = NULL;
	char *myserv = NULL;
	int myhostlen, myservlen;

	if (!(channel = netresolve_open()))
		return status;

	if ((query = netresolve_query_getnameinfo(channel, sa, salen, flags)))
		status = netresolve_query_getnameinfo_done(query, &myhost, &myserv, NULL);

	if (!status) {
		myhostlen = myhost ? strlen(myhost) + 1 : 0;
		myservlen = myserv ? strlen(myserv) + 1 : 0;
		if ((host && myhostlen > hostlen) || (serv && myservlen > servlen)) {
			status = EAI_OVERFLOW;
			goto out;
		}
		if (host) {
			memset(host, 0, hostlen);
			if (myhost)
				memcpy(host, myhost, myhostlen);
		}
		if (serv)
			memset(serv, 0, servlen);
			if (myserv)
				memcpy(serv, myserv, myservlen);
	}

out:
	netresolve_close(channel);
	return status;
}

/* gethostbyname2:
 *
 * Legacy extended version of `gethostbyname()`o lookup either IPv4 or IPv6 addresses for a
 * specified family. Result for AF_UNSPEC is not specified.
 *
 * Replaced by `getaddrinfo()` as follows:
 *
 *  * nodename = node
 *  * servname = NULL
 *  * hints.family = family
 *  * hints.socktype = SOCK_RAW
 *
 * See notes for `gethostbyname()`.
 *
 * GNU extention to POSIX.1-2001 `gethostbyname()`
 */
struct hostent *
gethostbyname2(const char *node, int family)
{
	netresolve_t channel;
	netresolve_query_t query;
	static struct hostent *he = NULL;

	if (!(channel = netresolve_open()))
		return NULL;

	if ((query = netresolve_query_gethostbyname(channel, node, family))) {
		netresolve_freehostent(he);
		he = netresolve_query_gethostbyname_done(query, &h_errno, NULL);
	}

	netresolve_close(channel);
	return he;
}

/* Legacy functions without a family parameter will only lookup IPv4. */
#define GETHOSTBYNAME_FAMILY AF_INET

/* gethostbyname:
 *
 * Legacy API to lookup IPv4 addresses for a specified hostname. Memory for
 * the result is allocated in a static buffer. The caller must not free it
 * and the function is not reentrant.
 *
 * Replaced by `getaddrinfo()` as follows:
 *
 *  * nodename = node
 *  * servname = NULL
 *  * hints.family = AF_INET
 *  * hints.socktype = SOCK_RAW
 *
 * Note: Using SOCK_RAW is not explicitly mandated by POSIX but it is required
 * by the GNU implementation.
 *
 * Defined in POSIX.1-2001, removed in POSIX.1-2008.
 */
struct hostent *
gethostbyname(const char *node)
{
	return gethostbyname2(node, GETHOSTBYNAME_FAMILY);
}

struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int type)
{
	static struct hostent *he = NULL;

	netresolve_t channel;
	netresolve_query_t query;

	if (!(channel = netresolve_open()))
		return NULL;

	if ((query = netresolve_query_gethostbyaddr(channel, addr, len, type))) {
		netresolve_freehostent(he);
		he = netresolve_query_gethostbyaddr_done(query, &h_errno, NULL);
	}

	netresolve_close(channel);
	return he;
}


/* gethostbyname2_r:
 *
 * Reentrant version of `gethostbyname2()`, see notes for `gethostbyname()`.
 */
int
gethostbyname2_r(const char *name, int family,
		struct hostent *he, char *buffer, size_t buflen,
		struct hostent **result, int *h_errnop)
{
	netresolve_t channel;
	netresolve_query_t query;
	struct hostent *tmp;

	if (!(channel = netresolve_open())) {
		*result = NULL;
		return errno;
	}

	if (!(query = netresolve_query_gethostbyname(channel, name, family))) {
		*result = NULL;
		netresolve_close(channel);
		return errno;
	}

	tmp = netresolve_query_gethostbyname_done(query, h_errnop, NULL);

	size_t len_name = tmp->h_name ? strlen(tmp->h_name) + 1 : 0;
	size_t count = 0;
	for (char **alias = tmp->h_aliases; *alias; alias++)
		count++;
	size_t len_aliases = (count + 1) * sizeof *tmp->h_aliases;
	count = 0;
	for (char **addr = tmp->h_addr_list; *addr; addr++)
		count++;
	size_t len_addr_list = (count + 1) * sizeof *tmp->h_addr_list;
	size_t len_addr_data = count * tmp->h_length;
	size_t len_total = len_aliases + len_name + len_addr_list + len_addr_data;

	if (len_total > buflen) {
		*result = NULL;
		return ERANGE;
	}

	memcpy(he, tmp, sizeof *he);
	memset(buffer, 0, buflen);
	he->h_name = buffer;
	he->h_aliases = (void *) buffer + len_name;
	he->h_addr_list = (void *) buffer + len_name + len_aliases;
	memcpy(he->h_name, tmp->h_name, len_name);
	/* we don't allocate space for actual aliases */
	memset(he->h_aliases, 0, len_aliases);
	memcpy(he->h_addr_list, tmp->h_addr_list, len_addr_list);
	for (int i = 0; tmp->h_addr_list[i]; i++) {
		he->h_addr_list[i] = buffer + len_name + len_aliases + len_addr_list + i * tmp->h_length;
		memcpy(he->h_addr_list[i], tmp->h_addr_list[i], he->h_length);
	}

	netresolve_freehostent(tmp);
	netresolve_close(channel);

	*result = he;

	return 0;
}

/* gethostbyname2_r:
 *
 * Reentrant version of `gethostbyname()`, see notes for `gethostbyname()`.
 */
int
gethostbyname_r(const char *name,
		struct hostent *he, char *buffer, size_t buflen,
		struct hostent **result, int *h_errnop)
{
	return gethostbyname2_r(name, GETHOSTBYNAME_FAMILY, he, buffer, buflen, result, h_errnop);
}

static int
_res_query(const char *dname, int class, int type, bool search, unsigned char *answer, int length)
{
	netresolve_t channel;
	netresolve_query_t query;
	const char *myanswer;
	size_t mylength = -1;

	if (!(channel = netresolve_open()))
		return mylength;

	if ((query = netresolve_query_dns(channel, dname, class, type))) {
		myanswer = netresolve_query_get_dns_answer(query, &mylength);

		if (mylength > length)
			goto out;

		memcpy(answer, myanswer, mylength);
	}

out:
	netresolve_query_done(query);
	netresolve_close(channel);
	return mylength;
}

int
res_query(const char *dname, int class, int type, unsigned char *answer, int anslen)
{
	return _res_query(dname, class, type, false, answer, anslen);
}

int
res_search(const char *dname, int class, int type, unsigned char *answer, int anslen)
{
	return _res_query(dname, class, type, true, answer, anslen);
}
