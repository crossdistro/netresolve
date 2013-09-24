#include <stdlib.h>
#include <unistd.h>

#include <netresolve-backend.h>

typedef struct {
	char *name;
	int family;
	union {
		struct in_addr address4;
		struct in6_addr address6;
	} address;
	int ifindex;
} Node;

static Node *nodes = NULL;
static int nodecount = 0, nodereservedcount = 0;

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

void
add_node(const char *name, int family, void *address, int ifindex)
{
	Node node;

	memset(&node, 0, sizeof node);
	if (name)
		node.name = strdup(name);
	node.family = family;
	if (address)
		memcpy(&node.address, address, family_to_length(family));

	if (nodecount == nodereservedcount) {
		if (!nodereservedcount)
			nodereservedcount = 256;
		else
			nodereservedcount *= 2;
		nodes = realloc(nodes, nodereservedcount * sizeof node);
	}

	memcpy(&nodes[nodecount++], &node, sizeof node);
}

void
read_node(char *line)
{
	const char *name;
	Address address;
	int family;
	int ifindex;

	if (!(netresolve_backend_parse_address(strtok(line, " \t"), &address, &family, &ifindex)))
		return;
	while ((name = strtok(NULL, " \t")))
		add_node(name, family, &address, ifindex);
}

#define HOSTS_FILE "/etc/hosts"
#define SIZE 1024

void
read_nodes(void)
{
	int fd = open(HOSTS_FILE, O_RDONLY);
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
				read_node(buffer);
				memmove(buffer, new, current - new);
				current = buffer + (current - new);
				new = buffer - 1;
			}
		}
	}
out:
	close(fd);
	add_node(NULL, 0, NULL, 0);
	nodereservedcount = nodecount;
	nodes = realloc(nodes, nodereservedcount * sizeof *nodes);
}

void
start(netresolve_backend_t resolver, char **settings)
{
	const char *request_node = netresolve_backend_get_node(resolver);
	const Node *node;
	int count = 0;

	if (!nodes)
		read_nodes();
	for (node = nodes; node->name; node++) {
		if (request_node && strcmp(request_node, node->name))
			continue;
		netresolve_backend_add_address(resolver, node->family, &node->address, node->ifindex);
		count++;
	}

	if (count)
		netresolve_backend_finished(resolver);
	else
		netresolve_backend_failed(resolver);
}
