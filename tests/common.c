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

	netresolve_query_get_node_info(query, 0, &family, &address, &ifindex);
	assert(family == exp_family);
	assert(!memcmp(address, exp_address, family == AF_INET6 ? 16 : 4));
	assert(ifindex == exp_ifindex);
}

void
callback1(netresolve_query_t query, void *user_data)
{
	struct priv_common *priv = user_data;

	check_address(query, AF_INET6, "1:2:3:4:5:6:7:8", 999999);

	priv->finished++;
}

void
callback2(netresolve_query_t query, void *user_data)
{
	struct priv_common *priv = user_data;

	check_address(query, AF_INET, "1.2.3.4", 999999);

	priv->finished++;
}
