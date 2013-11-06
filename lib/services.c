/* This file is part of the `netresolve` library.
 * Copyright (C) Pavel Šimerda, Red Hat, Inc. (psimerda at redhat.com) and others
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include <netresolve-backend.h>

typedef struct {
	int socktype;
	int protocol;
	bool defaultpair;
	const char *name;
} Protocol;

/* Order of socktype/protocol pairs is the same as in glibc's getaddrinfo()
 * implementation.
 */
static const Protocol protocols[] = {
	{ SOCK_STREAM, IPPROTO_TCP, true, "tcp" },
	{ SOCK_DGRAM, IPPROTO_UDP, true, "udp" },
	{ SOCK_DCCP, IPPROTO_DCCP, false, "dccp" },
	{ SOCK_DGRAM, IPPROTO_UDPLITE, false, "udplite" },
	{ SOCK_STREAM, IPPROTO_SCTP, false, "sctp" },
	{ SOCK_SEQPACKET, IPPROTO_SCTP, false, "sctp" },
	{ 0, 0, false, "" }
};

typedef struct {
	int protocol;
	int port;
	const char *name;
} Service;

static Service *services = NULL;
static int servicecount = 0, servicereservedcount = 0;

static int
protocol_from_string(const char *str)
{
	const Protocol *protocol;

	if (!str)
		return 0;
	for (protocol = protocols; protocol->protocol; protocol++)
		if (!strcmp(str, protocol->name))
			return protocol->protocol;
	return 0;
}

static void
add_service(int protocol, int port, const char *name)
{
	Service service;

	memset(&service, 0, sizeof service);
	service.protocol = protocol;
	service.port = port;
	service.name = strdup(name);

	if (servicecount == servicereservedcount) {
		if (!servicereservedcount)
			servicereservedcount = 256;
		else
			servicereservedcount *= 2;
		services = realloc(services, servicereservedcount * sizeof service);
	}

	memcpy(&services[servicecount++], &service, sizeof service);
}

static void
read_service(char *line)
{
	int protocol, port;
	const char *name, *alias;

	if (!(name = strtok(line, " \t")))
		return;
	if (!(port = strtol(strtok(NULL, "/"), NULL, 10)))
		return;
	if (!(protocol = protocol_from_string(strtok(NULL, " \t"))))
		return;
	add_service(protocol, port, name);
	while ((alias = strtok(NULL, " \t")))
		add_service(protocol, port, alias);
}

#define SERVICES_FILE "/etc/services"
#define SIZE 1024

static void
read_services(void)
{
	int fd = open(SERVICES_FILE, O_RDONLY);
	char buffer[SIZE];
	char *current = buffer;
	char *end = buffer + sizeof buffer;
	char *new;
	size_t size;

	if (fd == -1)
		return;

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
				read_service(buffer);
				memmove(buffer, new, current - new);
				current = buffer + (current - new);
				new = buffer - 1;
			}
		}
	}
out:
	close(fd);
	add_service(0, 0, "");
	servicereservedcount = servicecount;
	services = realloc(services, servicereservedcount * sizeof *services);
}

static void
found_port(void (*callback)(int, int, int, void *), void *user_data,
		int socktype, int proto, int portnum)
{
	const Protocol *protocol;

	for (protocol = protocols; protocol->protocol; protocol++) {
		if (socktype && socktype != protocol->socktype)
			continue;
		if (proto && proto != protocol->protocol)
			continue;
		if ((!socktype || !proto) && !protocol->defaultpair)
			continue;
		callback(protocol->socktype, protocol->protocol, portnum, user_data);
	}
}

void
_netresolve_get_service_info(void (*callback)(int, int, int, void *), void *user_data,
		const char *request_service, int socktype, int protocol)
{
	int port = 0;
	const Service *service;

	if (request_service) {
		char *endptr;

		port = strtol(request_service, &endptr, 10);
		if (!*endptr)
			found_port(callback, user_data, socktype, protocol, port);
		else {
			if (!services)
				read_services();
			for (service = services; service->name; service++) {
				if (protocol && protocol != service->protocol)
					continue;
				if (port && port != service->port)
					continue;
				if (!port && request_service && strcmp(request_service, service->name))
					continue;
				found_port(callback, user_data, socktype, service->protocol, service->port);
			}
		}
	} else
		callback(socktype, protocol, port, user_data);
}
