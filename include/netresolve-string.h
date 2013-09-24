#ifndef NETRESOLVE_STRING_H
#define NETRESOLVE_STRING_H

typedef struct netresolve_resolver *netresolve_t;

const char * netresolve_get_request_string(netresolve_t resolver);
const char * netresolve_get_response_string(netresolve_t resolver);

#endif /* NETRESOLVE_STRING_H */
