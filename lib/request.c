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

static void
copy_string(char **target, const char *source)
{
	free(*target);
	*target = source ? strdup(source) : NULL;
}

static void
copy_positive(int *target, int source)
{
	if (source > 0)
		*target = source;
}

bool
netresolve_request_set_options_from_va(struct netresolve_request *request, va_list ap)
{
	enum netresolve_option option;

	while ((option = va_arg(ap, enum netresolve_option))) {
		switch (option) {
		case NETRESOLVE_OPTION_NODE_NAME:
			copy_string(&request->nodename, va_arg(ap, const char *));
			break;
		case NETRESOLVE_OPTION_SERVICE_NAME:
			copy_string(&request->servname, va_arg(ap, const char *));
			break;
		case NETRESOLVE_OPTION_FAMILY:
			copy_positive(&request->family, va_arg(ap, int));
			break;
		case NETRESOLVE_OPTION_SOCKTYPE:
			copy_positive(&request->socktype, va_arg(ap, int));
			break;
		case NETRESOLVE_OPTION_PROTOCOL:
			copy_positive(&request->protocol, va_arg(ap, int));
			break;
		case NETRESOLVE_OPTION_IFINDEX:
			copy_positive(&request->ifindex, va_arg(ap, int));
			break;
		case NETRESOLVE_OPTION_IP4_ADDRESS:
			request->family = AF_INET;
			memcpy(request->address, va_arg(ap, const void *), sizeof (struct in_addr));
			break;
		case NETRESOLVE_OPTION_IP6_ADDRESS:
			request->family = AF_INET6;
			memcpy(request->address, va_arg(ap, const void *), sizeof (struct in6_addr));
			break;
		case NETRESOLVE_OPTION_PORT:
			copy_positive(&request->port, va_arg(ap, int));
			break;
		case NETRESOLVE_OPTION_DNS_NAME:
			copy_string(&request->dns_name, va_arg(ap, const char *));
			break;
		case NETRESOLVE_OPTION_DNS_CLASS:
			request->dns_class = va_arg(ap, int);
			break;
		case NETRESOLVE_OPTION_DNS_TYPE:
			request->dns_type = va_arg(ap, int);
			break;
		case NETRESOLVE_OPTION_DEFAULT_LOOPBACK:
			request->default_loopback = va_arg(ap, int);
			break;
		case NETRESOLVE_OPTION_DNS_SRV_LOOKUP:
			request->dns_srv_lookup = va_arg(ap, int);
			break;
		default:
			return false;
		}
	}

	return true;
}

static bool
get_option(struct netresolve_request *request, int option, void *argument)
{
	switch (option) {
	case NETRESOLVE_OPTION_DEFAULT_LOOPBACK:
		*(bool *) argument = request->default_loopback;
		break;
	case NETRESOLVE_OPTION_DNS_SRV_LOOKUP:
		*(bool *) argument = request->dns_srv_lookup;
		break;
	case NETRESOLVE_OPTION_NODE_NAME:
		*(const char **) argument = request->nodename;
		break;
	case NETRESOLVE_OPTION_SERVICE_NAME:
		*(const char **) argument = request->servname;
		break;
	case NETRESOLVE_OPTION_FAMILY:
		*(int *) argument = request->family;
		break;
	case NETRESOLVE_OPTION_SOCKTYPE:
		*(int *) argument = request->socktype;
		break;
	case NETRESOLVE_OPTION_PROTOCOL:
		*(int *) argument = request->protocol;
		break;
	case NETRESOLVE_OPTION_IFINDEX:
		*(int *) argument = request->ifindex;
		break;
	case NETRESOLVE_OPTION_IP4_ADDRESS:
	case NETRESOLVE_OPTION_IP6_ADDRESS:
		*(const void **) argument = request->address;
		break;
	case NETRESOLVE_OPTION_PORT:
		*(int *) argument = request->port;
		break;
	case NETRESOLVE_OPTION_DNS_NAME:
		*(const char **) argument = request->dns_name;
		break;
	case NETRESOLVE_OPTION_DNS_CLASS:
		*(int *) argument = request->dns_class;
		break;
	case NETRESOLVE_OPTION_DNS_TYPE:
		*(int *) argument = request->dns_type;
		break;
	default:
		return false;
	}

	return true;
}

bool
netresolve_request_get_options_from_va(struct netresolve_request *request, va_list ap)
{
	enum netresolve_option option;

	while ((option = va_arg(ap, enum netresolve_option)))
		if (!get_option(request, option, va_arg(ap, void *)))
			return false;

	return true;
}
