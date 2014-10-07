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
#include <netresolve-backend.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

#define SIZE (128*1024)

struct priv_nss {
	char *name;
	char *filename;
	const char *api;
	void *dl_handle;
	/* gethostbyname:
	 *
	 * Supports only IPv4.
	 */
	enum nss_status (*gethostbyname_r)(const char *name,
		struct hostent *host,
		char *buffer, size_t buflen,
		int *errnop, int *h_errnop);
	/* gethostbyname2:
	 *
	 * Offers an additional argument 'af' to choose between IPv4 and IPv6 name
	 * resolution, doesn't support mixed IPv4/IPv6
	 */
	enum nss_status (*gethostbyname2_r)(const char *name, int af,
		struct hostent *host,
		char *buffer, size_t buflen,
		int *errnop, int *h_errnop);
	/* gethostbyname3:
	 *
	 * Supports IPv4/IPv6 via 'af' like gethostbyname2(), 'ttlp' for returning
	 * the TTL, 'canonp' for returning the canonical name.
	 */
	enum nss_status (*gethostbyname3_r)(const char *name, int af,
		struct hostent *host,
		char *buffer, size_t buflen, int *errnop,
		int *h_errnop, int32_t *ttlp, char **canonp);
	/* gethostbyname4:
	 *
	 * Returns mixed IPv4/IPv6 and TTL via 'ttlp'. Doesn't support separate
	 * IPv4/IPv6 nor canonical name.
	 */
	enum nss_status (*gethostbyname4_r)(const char *name,
		struct gaih_addrtuple **pat,
		char *buffer, size_t buflen, int *errnop,
		int *h_errnop, int32_t *ttlp);
	struct hconf {
		int initialized;
		int unused1;
		int unused2[4];
		int num_trimdomains;
		const char *trimdomain[4];
		unsigned int flags;
	} *res_hconf;
	/* getaddrinfo:
	 *
	 * A getaddrinfo based API encorporating capabilities of all
	 * gethostbyname*_r APIs and more.
	 */
	int (*getaddrinfo)(const char *nodename, const char *servname,
		const struct addrinfo *hints,
		struct addrinfo **res,
		int32_t *ttlp);
};

static int
combine_statuses(int s4, int s6)
{
	/* NSS_STATUS_TRYAGAIN first, as it means we don't have the final version
	 * of the information.
	 */
	if (s4 == NSS_STATUS_TRYAGAIN || s6 == NSS_STATUS_TRYAGAIN)
		return NSS_STATUS_TRYAGAIN;
	/* NSS_STATUS_SUCCESS next, as e.g. libnss_mdns4's gethostbyname3 returns
	 * NSS_STATUS_UNAVAIL for family=AF_INET6 requests.
	 */
	if (s4 == NSS_STATUS_SUCCESS || s6 == NSS_STATUS_SUCCESS)
		return NSS_STATUS_SUCCESS;
	/* NSS_STATUS_NOTFOUND only accepted when returned for both protocols. */
	if (s4 == NSS_STATUS_NOTFOUND && s6 == NSS_STATUS_NOTFOUND)
		return NSS_STATUS_NOTFOUND;
	/* Fallback to NSS_STATUS_UNAVAIL. */
	return NSS_STATUS_UNAVAIL;
}

static void
try_symbol_pattern(netresolve_query_t query, struct priv_nss *priv, void **f, const char *pattern, const char *api)
{
	char symbol[1024] = { 0 };

	if (priv->api && strcmp(priv->api, api))
		return;

	snprintf(symbol, sizeof symbol, pattern, priv->name);
	*f = dlsym(priv->dl_handle, symbol);

	if (*f)
		debug("loaded %s (%s)\n", symbol, api);
	else
		debug("not loaded %s (%s): %s\n", symbol, api, dlerror());
}

static void
initialize(struct priv_nss *priv, netresolve_query_t query, char **settings)
{
	char *p;

	if (!settings || !*settings) {
		error("missing argument");
		return;
	}

	/* parse settings */
	priv->name = strdup(*settings++);
	if (*settings)
		priv->api = *settings++;
	if ((p = strrchr(priv->name, '/'))) {
		priv->filename = strdup(priv->name);
		p++;
		if (!strncmp(p, "libnss_", 7)) {
			p += 7;
			memmove(priv->name, p, strlen(p));
		}
		if ((p = strchr(priv->name, '.')))
			*p = '\0';
	} else
		if (asprintf(&priv->filename, "libnss_%s.so.2", priv->name) == -1)
			priv->filename = NULL;

	/* load nsswitch module */
	debug("loading NSS module: %s\n", priv->filename);
	priv->dl_handle = dlopen(priv->filename, RTLD_LAZY);
	if (!priv->dl_handle) {
		error("%s\n", dlerror());
		return;
	}

	/* find nsswitch entry points */
	try_symbol_pattern(query, priv, (void *) &priv->gethostbyname_r, "_nss_%s_gethostbyname_r", "gethostbyname");
	try_symbol_pattern(query, priv, (void *) &priv->gethostbyname2_r, "_nss_%s_gethostbyname2_r", "gethostbyname2");
	try_symbol_pattern(query, priv, (void *) &priv->gethostbyname3_r, "_nss_%s_gethostbyname3_r", "gethostbyname3");
	try_symbol_pattern(query, priv, (void *) &priv->gethostbyname4_r, "_nss_%s_gethostbyname4_r", "gethostbyname4");
	try_symbol_pattern(query, priv, (void *) &priv->res_hconf, "_res_hconf", "gethostbyname4");
	try_symbol_pattern(query, priv, (void *) &priv->getaddrinfo, "_nss_%s_getaddrinfo", "getaddrinfo");

	free(priv->name);
	priv->name = NULL;
	free(priv->filename);
	priv->filename = NULL;
}

static void
finalize(struct priv_nss *priv)
{
	dlclose(priv->dl_handle);
}

void
start(netresolve_query_t query, char **settings)
{
	const char *node = netresolve_backend_get_nodename(query);
	int family = netresolve_backend_get_family(query);
	struct priv_nss priv = { 0 };

	initialize(&priv, query, settings);

	if (!priv.dl_handle) {
		netresolve_backend_failed(query);
		return;
	}

	if (priv.getaddrinfo) {
		const char *service = netresolve_backend_get_servname(query);
		struct addrinfo hints = netresolve_backend_get_addrinfo_hints(query);
		int status;
		struct addrinfo *result;
		int32_t ttl;

		status = DL_CALL_FCT(priv.getaddrinfo, (node, service, &hints, &result, &ttl));
		netresolve_backend_apply_addrinfo(query, status, result, ttl);
		if (status == 0)
			freeaddrinfo(result);
	} else if (priv.gethostbyname4_r && family == AF_UNSPEC) {
		char buffer[SIZE] = { 0 };
		enum nss_status status;
		struct gaih_addrtuple *result;
		int errnop, h_errnop;
		int32_t ttl;

		/* Without this, libnss_files won't resolve using multiple records
		 * in /etc/hosts, e.g. won't return both IPv4 and IPv6 for "localhost"
		 * even when /etc/hosts is configured correctly.
		 *
		 * See relevant files in glibc sources:
		 *  - nss_files/files-hosts.c
		 *  - resolv/res_hconf.h
		 */
		if (priv.res_hconf)
			priv.res_hconf->flags = 16;

		status = DL_CALL_FCT(priv.gethostbyname4_r, (node, &result,
			buffer, sizeof buffer, &errnop, &h_errnop, &ttl));
		netresolve_backend_apply_addrtuple(query, status, result, ttl);
	} else if (node && (priv.gethostbyname3_r || priv.gethostbyname2_r)) {
		char buffer4[SIZE] = { 0 };
		char buffer6[SIZE] = { 0 };
		int status4 = NSS_STATUS_NOTFOUND, status6 = NSS_STATUS_NOTFOUND;
		struct hostent he4, he6;
		int errnop, h_errnop;
		int32_t ttl4 = 0;
		int32_t ttl6 = 0;
		char *canonname4 = NULL;
		char *canonname6 = NULL;

		if (priv.gethostbyname3_r) {
			if (family == AF_INET || family == AF_UNSPEC)
				status4 = DL_CALL_FCT(priv.gethostbyname3_r, (node, AF_INET,
					&he4, buffer4, sizeof buffer4, &errnop, &h_errnop, &ttl4, &canonname4));
			if (family == AF_INET6 || family == AF_UNSPEC)
				status6 = DL_CALL_FCT(priv.gethostbyname3_r, (node, AF_INET6,
					&he6, buffer6, sizeof buffer6, &errnop, &h_errnop, &ttl6, &canonname6));
		} else {
			if (family == AF_INET || family == AF_UNSPEC)
				status4 = DL_CALL_FCT(priv.gethostbyname2_r, (node, AF_INET,
					&he4, buffer4, sizeof buffer4, &errnop, &h_errnop));
			if (family == AF_INET6 || family == AF_UNSPEC)
				status6 = DL_CALL_FCT(priv.gethostbyname2_r, (node, AF_INET6,
					&he6, buffer6, sizeof buffer6, &errnop, &h_errnop));
		}

		if (combine_statuses(status4, status6) == NSS_STATUS_SUCCESS) {
			if (status6 == NSS_STATUS_SUCCESS && canonname6)
				netresolve_backend_set_canonical_name(query, canonname6);
			else if (status4 == NSS_STATUS_SUCCESS && canonname4)
				netresolve_backend_set_canonical_name(query, canonname4);
			if (status6 == NSS_STATUS_SUCCESS)
				netresolve_backend_apply_hostent(query, &he6, 0, 0, 0, 0, 0, ttl4);
			if (status4 == NSS_STATUS_SUCCESS)
				netresolve_backend_apply_hostent(query, &he4, 0, 0, 0, 0, 0, ttl6);
			netresolve_backend_finished(query);
		} else
			netresolve_backend_failed(query);
	} else if (node && priv.gethostbyname_r) {
		char buffer[SIZE];
		int errnop, h_errnop;
		struct hostent he;
		enum nss_status status;

		status = DL_CALL_FCT(priv.gethostbyname_r, (node,
			&he, buffer, sizeof buffer, &errnop, &h_errnop));

		if (status == NSS_STATUS_SUCCESS) {
			netresolve_backend_apply_hostent(query, &he, 0, 0, 0, 0, 0, 0);
			netresolve_backend_finished(query);
		} else
			netresolve_backend_failed(query);
	} else {
		debug("no suitable backend found\n");
		netresolve_backend_failed(query);
	}

	finalize(&priv);
}
