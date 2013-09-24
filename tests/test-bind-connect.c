#include <netresolve-utils.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

int
main(int argc, char **argv)
{
	int sock_server, sock_client, sock_accept;
	const char *node = NULL;
	const char *service = "exp1";
	int family = AF_INET;
	int socktype = SOCK_STREAM;
	int protocol = IPPROTO_TCP;
	int status;
	char outbuf[6] = "asdf\n";
	char inbuf[6] = {0};

	sock_server = netresolve_utils_bind(node, service, family, socktype, protocol);
	assert(sock_server > 0);
	status = listen(sock_server, 10);
	assert(status == 0);

	sock_client = netresolve_utils_connect(node, service, family, socktype, protocol);
	assert(sock_client > 0);

	sock_accept = accept(sock_server, NULL, 0);
	assert(sock_accept != -1);
	status = send(sock_client, outbuf, strlen(outbuf), 0);
	assert(status == strlen(outbuf));
	status = recv(sock_accept, inbuf, sizeof inbuf, 0);
	assert(status == strlen(outbuf));
	assert(!strcmp(inbuf, outbuf));

	status = close(sock_server);
	assert(status == 0);
	status = close(sock_client);
	assert(status == 0);

	return EXIT_SUCCESS;
}
