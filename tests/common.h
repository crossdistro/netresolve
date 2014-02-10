#include <netresolve.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


void check_address(netresolve_query_t query, int exp_family, const char *exp_address_str, int exp_ifindex);
