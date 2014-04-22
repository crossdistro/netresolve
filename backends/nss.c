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
#include <nss.h>
#include <dlfcn.h>

#define SIZE (128*1024)

struct priv_nss {
	char *name;
	char filename[1024];
	char *api;
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
};

static int
combine_statuses(int s4, int s6)
{
	/* NSS_STATUS_TRYAGAIN first, as it means something needs we don't have
	 * the final version of the information.
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
	char symbol[1024];

	if (priv->api && strcmp(priv->api, api)) {
		debug("%s API ignored: environment setting\n", api);
		return;
	}

	snprintf(symbol, sizeof symbol, pattern, priv->name);

	*f = dlsym(priv->dl_handle, symbol);

	if (!*f) {
		debug("%s API not loaded: %s\n", api, dlerror());
	}
}

void
start(netresolve_query_t query, char **settings)
{
	struct priv_nss *priv = netresolve_backend_new_priv(query, sizeof *priv);
	const char *node = netresolve_backend_get_nodename(query);
	int family = netresolve_backend_get_family(query);
	int status = NSS_STATUS_UNAVAIL;

	if (!priv || !settings || !*settings) {
		netresolve_backend_failed(query);
		return;
	}

	/* Load nsswitch backend: */
	priv->name = strdup(*settings);
	priv->api = secure_getenv("NETRESOLVE_NSS_API");
	snprintf(priv->filename, sizeof priv->filename, "libnss_%s.so.2", priv->name);
	priv->dl_handle = dlopen(priv->filename, RTLD_LAZY);
	if (!priv->dl_handle) {
		error("%s\n", dlerror());
		netresolve_backend_failed(query);
		return;
	}
	try_symbol_pattern(query, priv, (void *) &priv->gethostbyname_r, "_nss_%s_gethostbyname_r", "gethostbyname");
	try_symbol_pattern(query, priv, (void *) &priv->gethostbyname2_r, "_nss_%s_gethostbyname2_r", "gethostbyname2");
	try_symbol_pattern(query, priv, (void *) &priv->gethostbyname3_r, "_nss_%s_gethostbyname3_r", "gethostbyname3");
	try_symbol_pattern(query, priv, (void *) &priv->gethostbyname4_r, "_nss_%s_gethostbyname4_r", "gethostbyname4");

	/*if (priv->gethostbyname4_r) {
		TODO
	} else*/ if (node && (priv->gethostbyname3_r || priv->gethostbyname2_r)) {
		char buffer4[SIZE], buffer6[SIZE];
		int status4 = NSS_STATUS_NOTFOUND, status6 = NSS_STATUS_NOTFOUND;
		struct hostent he4, he6;
		int errnop, h_errnop;

		if (priv->gethostbyname3_r) {
			int32_t ttl;
			char *canonname = NULL;

			if (family == AF_INET || family == AF_UNSPEC)
				status4 = DL_CALL_FCT(priv->gethostbyname3_r, (node, AF_INET,
					&he4, buffer4, sizeof buffer4, &errnop, &h_errnop, &ttl, &canonname));
			if (family == AF_INET6 || family == AF_UNSPEC)
				status6 = DL_CALL_FCT(priv->gethostbyname3_r, (node, AF_INET6,
					&he6, buffer6, sizeof buffer6, &errnop, &h_errnop, &ttl, &canonname));
		} else {
			if (family == AF_INET || family == AF_UNSPEC)
				status4 = DL_CALL_FCT(priv->gethostbyname2_r, (node, AF_INET,
					&he4, buffer4, sizeof buffer4, &errnop, &h_errnop));
			if (family == AF_INET6 || family == AF_UNSPEC)
				status6 = DL_CALL_FCT(priv->gethostbyname2_r, (node, AF_INET6,
					&he6, buffer6, sizeof buffer6, &errnop, &h_errnop));
		}

		status = combine_statuses(status4, status6);
		if (status == NSS_STATUS_SUCCESS) {
			if (status4 == NSS_STATUS_SUCCESS)
				netresolve_backend_apply_hostent(query, &he4, 0, 0, 0, 0, 0);
			if (status6 == NSS_STATUS_SUCCESS)
				netresolve_backend_apply_hostent(query, &he6, 0, 0, 0, 0, 0);
		}
	} else if (node && priv->gethostbyname_r) {
		char buffer[SIZE];
		int errnop, h_errnop;
		struct hostent he;

		status = DL_CALL_FCT(priv->gethostbyname_r, (node,
			&he, buffer, sizeof buffer, &errnop, &h_errnop));

		if (status == NSS_STATUS_SUCCESS) {
			netresolve_backend_apply_hostent(query, &he, 0, 0, 0, 0, 0);
		}
	}

	if (status == NSS_STATUS_SUCCESS)
		netresolve_backend_finished(query);
	else
		netresolve_backend_failed(query);
}
