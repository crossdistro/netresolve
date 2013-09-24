#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <netresolve.h>
#include <netresolve-cli.h>
#include <netresolve-string.h>

int
main(int argc, char **argv)
{
	netresolve_t resolver;
	bool verbose;
	int status;

	argv++;
	if (*argv && !strcmp(*argv, "-v")) {
		verbose = true;
		argv++;
	}

	resolver = netresolve_open();
	if (!resolver) {
		fprintf(stderr, "netresolve: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	if (verbose)
		netresolve_set_log_level(resolver, 10);

	status = netresolve_resolve_argv(resolver, argv);
	if (status) {
		fprintf(stderr, "netresolve: %s\n", strerror(status));
		return EXIT_FAILURE;
	}

	if (verbose)
		fprintf(stderr, "%s", netresolve_get_request_string(resolver));
	printf("%s", netresolve_get_response_string(resolver));
	netresolve_close(resolver);
	return EXIT_SUCCESS;
}
