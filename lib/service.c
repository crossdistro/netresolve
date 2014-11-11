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
#include <netresolve-private.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>

struct netresolve_protocol {
	int socktype;
	int protocol;
	bool defaultpair;
	const char *name;
};

/* Order of socktype/protocol pairs is the same as in glibc's getaddrinfo()
 * implementation.
 */
static const struct netresolve_protocol protocols[] = {
	{ SOCK_STREAM, IPPROTO_TCP, true, "tcp" },
	{ SOCK_DGRAM, IPPROTO_UDP, true, "udp" },
	{ SOCK_DCCP, IPPROTO_DCCP, false, "dccp" },
	{ SOCK_DGRAM, IPPROTO_UDPLITE, false, "udplite" },
	{ SOCK_STREAM, IPPROTO_SCTP, false, "sctp" },
	{ SOCK_SEQPACKET, IPPROTO_SCTP, false, "sctp" },
	{ 0, 0, false, "" }
};

struct netresolve_service {
	int protocol;
	int port;
	const char *name;
};

struct netresolve_service_list {
	struct netresolve_service *items;
	size_t count;
	size_t reserved;

};

static int
protocol_from_string(const char *str)
{
	const struct netresolve_protocol *protocol;

	if (!str)
		return 0;
	for (protocol = protocols; protocol->protocol; protocol++)
		if (!strcmp(str, protocol->name))
			return protocol->protocol;
	return 0;
}

static void
add_service(struct netresolve_service_list *services, int protocol, int port, const char *name)
{
	struct netresolve_service service;

	memset(&service, 0, sizeof service);
	service.protocol = protocol;
	service.port = port;
	if (name)
		service.name = strdup(name);

	if (services->count == services->reserved) {
		if (!services->reserved)
			services->reserved = 256;
		else
			services->reserved *= 2;
		services->items = realloc(services->items, services->reserved * sizeof service);
	}

	memcpy(&services->items[services->count++], &service, sizeof service);
}

static void
read_service(struct netresolve_service_list *services, char *line)
{
	int protocol, port;
	const char *name, *alias;

	if (!(name = strtok(line, " \t")))
		return;
	if (!(port = strtol(strtok(NULL, "/"), NULL, 10)))
		return;
	if (!(protocol = protocol_from_string(strtok(NULL, " \t"))))
		return;
	add_service(services, protocol, port, name);
	while ((alias = strtok(NULL, " \t")))
		add_service(services, protocol, port, alias);
}

#define SIZE 1024

static void
read_services(struct netresolve_service_list *services, const char *path)
{
	int fd;
	char buffer[SIZE];
	char *current = buffer;
	char *end = buffer + sizeof buffer;
	char *new;
	size_t size;

	if (path) {
		if ((fd = open(path, O_RDONLY)) == -1)
			return;
	} else {
		if ((fd = open("/etc/netresolve/services", O_RDONLY)) == -1 && (fd = open("/etc/services", O_RDONLY)) == -1)
			return;
	}

	while (true) {
		if (current == end)
			goto out;

		size = read(fd, current, end-current);

		if (size == 0)
			goto out;
		if (size == -1)
			goto out;

		current += size;

		/* buffer ...record... new ...next... current ...empty... end */
		for (new = buffer; new < current; new++) {
			/* Comment */
			if (*new == '#')
				*new = '\0';
			/* New line */
			if (*new == '\n') {
				*new++ = '\0';
				read_service(services, buffer);
				memmove(buffer, new, current - new);
				current = buffer + (current - new);
				new = buffer - 1;
			}
		}
	}
out:
	close(fd);
	add_service(services, 0, 0, NULL);
	services->reserved = services->count;
	services->items = realloc(services->items, services->reserved * sizeof *services->items);
}

struct netresolve_service_list *
netresolve_service_list_new(const char *path)
{
	struct netresolve_service_list *services;

	if (!path)
		path = secure_getenv("NETRESOLVE_SERVICES");

	if (!(services = calloc(1, sizeof *services)))
		return NULL;

	read_services(services, path);

	return services;
}

void
netresolve_service_list_free(struct netresolve_service_list *services)
{
	if (!services)
		return;

	free(services->items);
	free(services);
}

static void
found_port(const char *name, int socktype, int proto, int port,
		netresolve_service_callback callback, void *user_data)
{
	const struct netresolve_protocol *protocol;

	for (protocol = protocols; protocol->protocol; protocol++) {
		if (socktype && socktype != protocol->socktype)
			continue;
		if (proto && proto != protocol->protocol)
			continue;
		if ((!socktype || !proto) && !protocol->defaultpair)
			continue;
		callback(name, protocol->socktype, protocol->protocol, port, user_data);
	}
}

void
netresolve_service_list_query(struct netresolve_service_list **services,
		const char *name, int socktype, int protocol, int port,
		netresolve_service_callback callback, void *user_data)
{
	const struct netresolve_service *service;
	int count = 0;

	if (name && !port) {
		char *endptr;

		port = strtol(name, &endptr, 10);
		if (!*endptr) {
			found_port(name, socktype, protocol, port, callback, user_data);
			return;
		}
	}

	*services = netresolve_service_list_new(NULL);

	if (*services && (*services)->items) {
		for (service = (*services)->items; service->name; service++) {
			if (name && strcmp(name, service->name))
				continue;
			if (protocol && protocol != service->protocol)
				continue;
			if ((port || !name) && port != service->port)
				continue;
			count++;
			found_port(service->name, socktype, service->protocol, service->port, callback, user_data);
		}
	}

	if (!count) {
		char buffer[128] = { 0 };

		snprintf(buffer, sizeof buffer, "%d", (int) port);
		found_port(port ? buffer : NULL, socktype, protocol, port, callback, user_data);
	}
}
