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
#include <netresolve-private.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>

static bool
getenv_bool(const char *name, bool def)
{
	const char *value = secure_getenv(name);

	return value ? (!strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "1")) : def;
}

static int
getenv_int(const char *name, int def)
{
	const char *value = secure_getenv(name);

	return value ? strtoll(value, NULL, 10) : def;
}

static int
getenv_family(const char *name, int def)
{
	const char *value = secure_getenv(name);

	return value ? netresolve_family_from_string(value) : def;
}

netresolve_t
netresolve_context_new(void)
{
	netresolve_t context;

	/* FIXME: this should probably be only called once */
	if (getenv_bool("NETRESOLVE_VERBOSE", false))
		netresolve_set_log_level(NETRESOLVE_LOG_LEVEL_DEBUG);

	if (!(context = calloc(1, sizeof *context)))
		return NULL;

	context->queries.previous = context->queries.next = &context->queries;
	context->epoll.fd = -1;

	context->config.force_family = getenv_family("NETRESOLVE_FORCE_FAMILY", AF_UNSPEC);

	context->request.default_loopback = getenv_bool("NETRESOLVE_FLAG_DEFAULT_LOOPBACK", false);
	context->request.clamp_ttl = getenv_int("NETRESOLVE_CLAMP_TTL", -1);
	context->request.request_timeout = getenv_int("NETRESOLVE_REQUEST_TIMEOUT", 15000);
	context->request.result_timeout = getenv_int("NETRESOLVE_RESULT_TIMEOUT", 5000);

	return context;
}

void
netresolve_context_free(netresolve_t context)
{
	struct netresolve_query *queries = &context->queries;

	debug_context(context, "destroying context");

	while (queries->next != queries)
		netresolve_query_free(queries->next);

	netresolve_set_backend_string(context, "");

	if (context->callbacks.cleanup)
		context->callbacks.cleanup(context->callbacks.user_data);

	memset(context, 0, sizeof *context);
	free(context);
}

void
netresolve_context_set_options(netresolve_t context, ...)
{
	va_list ap;

	va_start(ap, context);
	netresolve_request_set_options_from_va(&context->request, ap);
	va_end(ap);
}

static void
free_backend(struct netresolve_backend *backend)
{
	char **p;

	if (!backend)
		return;
	if (backend->settings) {
		for (p = backend->settings; *p; p++)
			free(*p);
		free(backend->settings);
	}
	if (backend->dl_handle)
		dlclose(backend->dl_handle);
	free(backend);
}

static struct netresolve_backend *
load_backend(char **take_settings)
{
	struct netresolve_backend *backend = calloc(1, sizeof *backend);
	const char *name;
	char filename[1024];

	if (!backend)
		return NULL;
	if (!take_settings || !*take_settings)
		goto fail;

	name = *take_settings;
	if (*name == '+') {
		backend->mandatory = true;
		name++;
	}

	backend->settings = take_settings;
	snprintf(filename, sizeof filename, "libnetresolve-backend-%s.so", name);
	backend->dl_handle = dlopen(filename, RTLD_NOW);
	if (!backend->dl_handle) {
		error("%s", dlerror());
		goto fail;
	}

	backend->setup[NETRESOLVE_REQUEST_FORWARD] = dlsym(backend->dl_handle, "query_forward");
	backend->setup[NETRESOLVE_REQUEST_REVERSE] = dlsym(backend->dl_handle, "query_reverse");
	backend->setup[NETRESOLVE_REQUEST_DNS] = dlsym(backend->dl_handle, "query_dns");

	return backend;
fail:
	free_backend(backend);
	return NULL;
}

void
netresolve_set_backend_string(netresolve_t context, const char *string)
{
	const char *setup, *end;
	char **settings = NULL;
	int nsettings = 0;
	int nbackends = 0;

	/* Default */
	if (string == NULL)
		string =
			"unix|any|loopback|numerichost|hosts|hostname|avahi"
#if defined(USE_ARES)
			"|aresdns"
#elif defined(USE_UNBOUND)
			"|ubdns"
#endif
			;

	/* Clear old backends */
	if (context->backends) {
		struct netresolve_backend **backend;

		for (backend = context->backends; *backend; backend++)
			free_backend(*backend);
		free(context->backends);
		context->backends = NULL;
	}

	/* Empty string suggest we only clean up. */
	if (!*string)
		return;

	/* Install new set of backends */
	for (setup = end = string; true; end++) {
		if (*end == ' ' || *end == '|' || *end == '\0') {
			settings = realloc(settings, (nsettings + 2) * sizeof *settings);
			settings[nsettings++] = strndup(setup, end - setup);
			settings[nsettings] = NULL;
			setup = end + 1;
		}
		if (*end == '|' || *end == '\0') {
			if (settings && *settings && **settings) {
				context->backends = realloc(context->backends, (nbackends + 2) * sizeof *context->backends);
				context->backends[nbackends] = load_backend(settings);
				if (context->backends[nbackends]) {
					nbackends++;
					context->backends[nbackends] = NULL;
				}
			} else
				free(settings);
			nsettings = 0;
			settings = NULL;
		}
		if (*end == '\0') {
			break;
		}
	}
}
