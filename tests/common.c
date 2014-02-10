#include "common.h"

void
check_address(netresolve_query_t query, int exp_family, const char *exp_address_str, int exp_ifindex)
{
	unsigned char exp_address[16] = { 0 };
	int family;
	const void *address;
	int ifindex;

	inet_pton(exp_family, exp_address_str, exp_address);

	assert(query);
	assert(netresolve_query_get_count(query) == 1);

	netresolve_query_get_address_info(query, 0, &family, &address, &ifindex);
	assert(family = exp_family);
	assert(!memcmp(address, exp_address, family == AF_INET6 ? 16 : 4));
	assert(ifindex == exp_ifindex);
}
