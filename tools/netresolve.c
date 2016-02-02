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
#include <getopt.h>
#include <arpa/nameser.h>
#include <netinet/ip_icmp.h>
#include <linux/icmpv6.h>
#include <sys/socket.h>

#ifdef USE_LDNS
#include <ldns/ldns.h>
#endif

static int
count_argv(char **argv)
{
	int count = 0;

	while (*argv++)
		count++;

	return count;
}

static void
read_and_write(int rfd, int wfd, int sock)
{
	char buffer[1024];
	ssize_t rsize, wsize, offset;

	rsize = read(rfd, buffer, sizeof(buffer));
	if (rsize == -1) {
		error("read: %s", strerror(errno));
		abort();
	}
	if (rsize == 0) {
		debug("end of input\n");
		shutdown(sock, SHUT_RDWR);
		exit(0);
	}
	for (offset = 0; offset < rsize; offset += wsize) {
		debug("%s: <<<%*s>>>\n",
				(rfd == 0) ? "sending" : "receiving",
				(int) (rsize - offset), buffer + offset);
		wsize = write(wfd, buffer + offset, rsize - offset);
		if (wsize <= 0) {
			debug("write: %s\n", strerror(errno));
			abort();
		}
	}
}

static void
on_socket(netresolve_query_t query, int idx, int sock, void *user_data)
{
	int *fd = user_data;;

	if (*fd == -1)
		*fd = sock;
}

static void
on_accept(netresolve_query_t query, int idx, int sock, void *user_data)
{
	on_socket(query, idx, sock, user_data);

	netresolve_listen_free(query);
}

static char *
get_dns_string(netresolve_query_t query)
{
#ifdef USE_LDNS
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
#else
	return NULL;
#endif
}

static const char *
sa_to_string(struct sockaddr *sender)
{
	static char buffer[1024];

	switch (sender->sa_family) {
	case AF_INET:
		return inet_ntop(AF_INET, &((struct sockaddr_in *) sender)->sin_addr, buffer, sizeof buffer);
	case AF_INET6:
		return inet_ntop(AF_INET6, &((struct sockaddr_in6 *) sender)->sin6_addr, buffer, sizeof buffer);
	default:
		return NULL;
	}
}

bool
run_ping(netresolve_query_t query, size_t idx)
{
	struct pollfd sock = { .fd = -1, .events = POLLIN };
	const struct sockaddr *sa;
	socklen_t salen;
	struct icmphdr data4 = { .type = ICMP_ECHO };
	struct icmp6hdr data6 = { .icmp6_type = ICMPV6_ECHO_REQUEST };
	int status;
	char buffer[4096];
	struct sockaddr_storage sender;

	sa = netresolve_query_get_sockaddr(query, idx, &salen, NULL, NULL, NULL);
	if (!sa)
		return false;

	sock.fd = socket(sa->sa_family, SOCK_DGRAM, sa->sa_family == AF_INET ? IPPROTO_ICMP : IPPROTO_ICMPV6);
	if (sock.fd == -1)
		return false;

	while (true) {
		if (sa->sa_family == AF_INET)
			status = sendto(sock.fd, &data4, sizeof data4, 0, sa, salen);
		else
			status = sendto(sock.fd, &data6, sizeof data6, 0, sa, salen);
		if (status == -1)
			return false;
		fprintf(stderr, "echo request\n");

		status = poll(&sock, 1, -1);
		if (status != 1)
			return false;
		status = recvfrom(sock.fd, buffer, sizeof buffer, 0, (struct sockaddr *) &sender, &salen);
		if (status == -1)
			return false;
		query = netresolve_query_getnameinfo(NULL, (struct sockaddr *) &sender, salen, 0, NULL, NULL);
		if (!query)
			return false;
		fprintf(stderr, "reply from %s (%s)\n", netresolve_query_get_node_name(query), sa_to_string((struct sockaddr *) &sender));

		sleep(1);
	}

	return true;
}

void
usage(void)
{
	fprintf(stderr,
			"netresolve [ OPTIONS ]\n"
			"\n"
			"Forward query:\n"
			"  -n,--node <nodename> -- node name\n"
			"  -s,--service <servname> -- service name\n"
			"  -f,--family any|ip4|ip6 -- family name\n"
			"  -t,--socktype any|stream|dgram|seqpacket -- socket type\n"
			"  -p,--protocol any|tcp|udp|sctp -- transport protocol\n"
			"  -S,--srv -- use SRV records\n"
			"\n"
			"Reverse query:\n"
			"  -a,--address -- IPv4/IPv6 address (reverse query)\n"
			"  -P,--port -- TCP/UDP port\n"
			"\n"
			"DNS query:\n"
			"  -C,--class -- DNS record class\n"
			"  -T,--type -- DNS record type\n"
			"\n"
			"Socket API:\n"
			"  -l,--listen -- attempt to listen on a port like netcat/socat\n"
			"  -c,--connect -- attempt to connect to a host like netcat/socat\n"
			"  --ping -- perform an ICMP ping"
			"\n"
			"Miscellaneous:\n"
			"  -b,--backends <backends> -- comma-separated list of backends\n"
			"  -h,--help -- help\n"
			"  -v,--verbose -- show more verbose output\n"
			"\n"
			"Examples:\n"
			"  netresolve --node www.sourceforge.net --service http\n"
			"  netresolve --address 8.8.8.8 --port 80\n"
			"  netresolve --node jabber.org --type SRV\n");
	exit(EXIT_SUCCESS);
}

int
main(int argc, char **argv)
{
	static const struct option longopts[] = {
		{ "help", 0, 0, 'h' },
		{ "verbose", 0, 0, 'v' },
		{ "listen", 0, 0, 'l' },
		{ "connect", 0, 0, 'c' },
		{ "node", 1, 0, 'n' },
		{ "host", 1, 0, 'n' },
		{ "service", 1, 0, 's' },
		{ "family", 1, 0, 'f' },
		{ "socktype", 1, 0, 't' },
		{ "ping", 0, 0, 'I' },
		{ "protocol", 1, 0, 'p' },
		{ "backends", 1, 0, 'b' },
		{ "srv", 0, 0, 'S' },
		{ "address", 1, 0, 'a' },
		{ "port", 1, 0, 'P' },
		{ "class", 1, 0, 'C' },
		{ "type", 1, 0, 'T' },
		{ NULL, 0, 0, 0 }
	};
	static const char *opts = "hvcn::s:f:t:p:b:Sa:P:";
	int opt, idx = 0;
	bool do_connect = false;
	bool do_listen = false;
	bool ping = false;
	char *nodename = NULL, *servname = NULL;
	char *address_str = NULL, *port_str = NULL;
	int cls = ns_c_in, type = 0;
	netresolve_t context;
	netresolve_query_t query;

	netresolve_set_log_level(NETRESOLVE_LOG_LEVEL_ERROR);

	context = netresolve_context_new();
	if (!context) {
		error("netresolve: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	while ((opt = getopt_long(count_argv(argv), argv, opts, longopts, &idx)) != -1) {
		switch (opt) {
		case 'h':
			usage();
		case 'v':
			netresolve_set_log_level(NETRESOLVE_LOG_LEVEL_DEBUG);
			break;
		case 'l':
			do_listen = true;
			break;
		case 'c':
			do_connect = true;
			break;
		case 'I':
			ping = true;
			break;
		case 'n':
			nodename = optarg;
			break;
		case 's':
			servname = optarg;
			break;
		case 'f':
			netresolve_context_set_options(context,
					NETRESOLVE_OPTION_FAMILY, netresolve_family_from_string(optarg),
					NETRESOLVE_OPTION_DONE);
			break;
		case 't':
			netresolve_context_set_options(context,
					NETRESOLVE_OPTION_SOCKTYPE, netresolve_socktype_from_string(optarg),
					NETRESOLVE_OPTION_DONE);
			break;
		case 'p':
			netresolve_context_set_options(context,
					NETRESOLVE_OPTION_PROTOCOL, netresolve_protocol_from_string(optarg),
					NETRESOLVE_OPTION_DONE);
			break;
		case 'b':
			netresolve_set_backend_string(context, optarg);
			break;
		case 'S':
			netresolve_context_set_options(context,
					NETRESOLVE_OPTION_DNS_SRV_LOOKUP, (int) true,
					NETRESOLVE_OPTION_DONE);
			break;
		case 'a':
			address_str = optarg;
			break;
		case 'P':
			port_str = optarg;
			break;
		case 'C':
#ifdef USE_LDNS
			cls = ldns_get_rr_class_by_name(optarg);
#else
			cls = strtoll(optarg, NULL, 10);
#endif
			break;
		case 'T':
#ifdef USE_LDNS
			type = ldns_get_rr_type_by_name(optarg);
#else
			type = strtoll(optarg, NULL, 10);
#endif
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}

	if (argv[optind])
		usage();

	if (do_listen || do_connect) {
		netresolve_query_t query;
		int sock = -1;
		struct pollfd fds[2];

		/* Linux: I found an interesting inconsistency where zero socktype
		 * is supported by the kernel but not when combined with
		 * `SOCK_NONBLOCK` which is internally used by netresolve
		 * socket API implementation.
		 */
		if (!context->request.socktype && !context->request.protocol) {
			context->request.socktype = SOCK_STREAM;
			context->request.protocol = IPPROTO_TCP;
		}

		if (do_listen) {
			if (!(query = netresolve_listen(context, nodename, servname, 0, 0, 0))) {
				error("netresolve: Cannot create listening socket: %s", strerror(errno));
				return EXIT_FAILURE;
			}

			netresolve_accept(query, on_accept, &sock);
		} else {
			query = netresolve_connect(context, nodename, servname, 0, 0, 0, on_socket, &sock);

			while (sock == -1) {
				error("netresolve: Socket connection failed: %s", strerror(errno));

				if (errno == ENETUNREACH) {
					netresolve_connect_next(query);
					continue;
				}

				return EXIT_FAILURE;
			}

			netresolve_connect_free(query);
		}

		debug("Connected.");

		fds[0].fd = 0;
		fds[0].events = POLLIN;
		fds[1].fd = sock;
		fds[1].events = POLLIN;

		while (true) {
			if (poll(fds, 2, -1) == -1) {
				fprintf(stderr, "poll: %s\n", strerror(errno));
				break;
			}

			if (fds[0].revents & POLLIN)
				read_and_write(0, sock, sock);
			if (fds[1].revents & POLLIN)
				read_and_write(sock, 1, sock);
		}

		return EXIT_SUCCESS;
	} else if (type)
		query = netresolve_query_dns(context, nodename, cls, type, NULL, NULL);
	else if (address_str || port_str) {
		Address address;
		int family, ifindex;

		if (!netresolve_backend_parse_address(address_str, &address, &family, &ifindex))
			return EXIT_FAILURE;
		query = netresolve_query_reverse(context, family, &address, ifindex, -1, port_str ? strtol(port_str, NULL, 10) : 0, NULL, NULL);
	} else
		query = netresolve_query_forward(context, nodename, servname, NULL, NULL);

	if (!query) {
		fprintf(stderr, "netresolve: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	debug("%s", netresolve_get_request_string(query));

	if (ping) {
		for (int i = 0; i < netresolve_query_get_count(query); i++)
			if (run_ping(query, i))
				goto out;
		error("netresolve: ping failed");
		goto out;
	}

	const char *response_string = netresolve_get_response_string(query);
	char *dns_string = get_dns_string(query);

	if (response_string)
		printf("%s", response_string);
	if (dns_string) {
		printf("%s", dns_string);
		free(dns_string);
	}

out:
	netresolve_context_free(context);
	return EXIT_SUCCESS;
}
