#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int
main(int argc, char **argv)
{
	const char *node = "1:2:3:4:5:6:7:8%999999";
	const char *service = "80";
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = 0,
		.ai_protocol = IPPROTO_TCP,
		.ai_flags = 0,
	};
	struct addrinfo *result = NULL;
	int status;

	status = getaddrinfo(node, service, &hints, &result);
	assert(status == 0);
	assert(result);
	assert(!result->ai_next);
}
