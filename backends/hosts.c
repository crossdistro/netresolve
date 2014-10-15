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
#include <unistd.h>

struct hosts_list {
	struct hosts_item *items;
	int count;
	int reserved;
};

struct hosts_item {
	char *name;
	int family;
	union {
		struct in_addr address4;
		struct in6_addr address6;
	} address;
	int ifindex;
};

static size_t
family_to_length(int family)
{
	switch (family) {
	case AF_INET:
		return 4;
	case AF_INET6:
		return 16;
	default:
		return 0;
	}
}

static void
add_node(struct hosts_list *list, const char *name, int family, void *address, int ifindex)
{
	struct hosts_item item;

	memset(&item, 0, sizeof item);
	if (name)
		item.name = strdup(name);
	item.family = family;
	if (address)
		memcpy(&item.address, address, family_to_length(family));

	if (list->count == list->reserved) {
		if (!list->reserved)
			list->reserved = 256;
		else
			list->reserved *= 2;
		list->items = realloc(list->items, list->reserved * sizeof item);
	}

	memcpy(&list->items[list->count++], &item, sizeof item);
}

static void
read_item(struct hosts_list *list, char *line)
{
	const char *name;
	Address address;
	int family;
	int ifindex;

	if (!(netresolve_backend_parse_address(strtok(line, " \t"), &address, &family, &ifindex)))
		return;
	while ((name = strtok(NULL, " \t")))
		add_node(list, name, family, &address, ifindex);
}

#define HOSTS_FILE "/etc/hosts"
#define BUFFER_SIZE 1024

static void
read_list(struct hosts_list *list)
{
	int fd = open(HOSTS_FILE, O_RDONLY);
	char buffer[BUFFER_SIZE];
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
				read_item(list, buffer);
				memmove(buffer, new, current - new);
				current = buffer + (current - new);
				new = buffer - 1;
			}
		}
		
	}
out:
	close(fd);
	add_node(list, NULL, 0, NULL, 0);
	list->reserved = list->count;
	list->items = realloc(list->items, list->reserved * sizeof *list->items);
}

void
setup_forward(netresolve_query_t query, char **settings)
{
	const char *node = netresolve_backend_get_nodename(query);
	struct hosts_list list;
	struct hosts_item *item;
	int count = 0;

	if (!node) {
		netresolve_backend_failed(query);
		return;
	}

	/* TODO: Would be nice to read the hosts once in a thread-safe way and with timestamp checking. */
	memset(&list, 0, sizeof list);
	read_list(&list);

	for (item = list.items; item->name; item++) {
		if (strcmp(node, item->name))
			continue;
		netresolve_backend_add_path(query, item->family, &item->address, item->ifindex, 0, 0, 0, 0, 0, 0);
		count++;
	}

	if (count)
		netresolve_backend_finished(query);
	else
		netresolve_backend_failed(query);

	free(list.items);
}
