#include <netresolve-private.h>
#include <netresolve-compat.h>
#include <netresolve-epoll.h>
//#include <libasyncns.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>

typedef struct netresolve_asyncns asyncns_t;
typedef struct netresolve_asyncns_query asyncns_query_t;

struct netresolve_asyncns_query {
	asyncns_t *asyncns;
	netresolve_query_t query;
	bool done;
	void *userdata;
	asyncns_query_t *previous;
	asyncns_query_t *next;
};

struct netresolve_asyncns {
	netresolve_t context;
	asyncns_query_t queries;
};

static void
enqueue(asyncns_query_t *list, asyncns_query_t *q)
{
	q->previous = list->previous;
	q->next = list;

	q->previous->next = q;
	q->next->previous = q;
}

static void
dequeue(asyncns_query_t *q)
{
	q->previous->next = q->next;
	q->next->previous = q->previous;
}

static void
callback(netresolve_query_t query, void *user_data)
{
	asyncns_query_t *q = user_data;

	q->done = true;
}

asyncns_t *
asyncns_new (unsigned n_proc)
{
	asyncns_t *asyncns;

	if (!(asyncns = calloc(1, sizeof *asyncns)))
		goto fail_asyncns;

    if (!(asyncns->context = netresolve_epoll_new()))
		goto fail_context;

	asyncns->queries.previous = asyncns->queries.next = &asyncns->queries;

	return asyncns;
fail_context:
	free(asyncns);
fail_asyncns:
	return NULL;
}

int
asyncns_fd(asyncns_t *asyncns)
{
	return netresolve_epoll_fd(asyncns->context);
}

int
asyncns_wait(asyncns_t *asyncns, int block)
{
	if (block)
		netresolve_epoll_wait(asyncns->context);
	else
		netresolve_epoll_dispatch(asyncns->context);

	/* undocumented return value */
	return 0;
}

static asyncns_query_t *
add_query(asyncns_t *asyncns, asyncns_query_t *q, netresolve_query_t query)
{
	if (!query) {
		free(q);
		return NULL;
	}

	q->asyncns = asyncns;
	q->query = query;

	enqueue(&asyncns->queries, q);

	return q;
}

static netresolve_query_t
remove_query(asyncns_query_t *q)
{
	netresolve_query_t query = q->query;

	dequeue(q);
	free(q);

	return query;
}

asyncns_query_t *
asyncns_getaddrinfo(asyncns_t *asyncns, const char *node, const char *service, const struct addrinfo *hints) 
{
	asyncns_query_t *q;

	if (!(q = calloc(1, sizeof *q)))
		return NULL;

	return add_query(asyncns, q, netresolve_query_getaddrinfo(asyncns->context, node, service, hints, callback, q));
}

int
asyncns_getaddrinfo_done (asyncns_t *asyncns, asyncns_query_t *q, struct addrinfo **result)
{
	return netresolve_query_getaddrinfo_done(remove_query(q), result, NULL);
}

asyncns_query_t *
asyncns_getnameinfo (asyncns_t *asyncns, const struct sockaddr *sa, socklen_t salen, int flags, int gethost, int getserv)
{
	asyncns_query_t *q;

	if (!(q = calloc(1, sizeof *q)))
		return NULL;

	return add_query(asyncns, q, netresolve_query_getnameinfo(asyncns->context, sa, salen, flags, callback, q));
}

int
asyncns_getnameinfo_done (asyncns_t *asyncns, asyncns_query_t *q, char *host, size_t hostlen, char *serv, size_t servlen)
{
	char *myhost;
	char *myserv;
	int status;

	status = netresolve_query_getnameinfo_done(remove_query(q), &myhost, &myserv, NULL);

	if (!status) {
		size_t myhostlen = strlen(myhost) + 1;
		size_t myservlen = strlen(myserv) + 1;

		if (hostlen <= myhostlen && servlen <= myservlen) {
			memcpy(host, myhost, myhostlen);
			memcpy(serv, myserv, myservlen);
		} else
			status = ERANGE;

		free(myhost);
		free(myserv);
	}

	return status;
}

asyncns_query_t *
asyncns_res_query (asyncns_t *asyncns, const char *dname, int class, int type)
{
	asyncns_query_t *q;

	if (!(q = calloc(1, sizeof *q)))
		return NULL;

	return add_query(asyncns, q, netresolve_query_dns(asyncns->context, dname, class, type, callback, q));
}

asyncns_query_t *
asyncns_res_search (asyncns_t *asyncns, const char *dname, int class, int type)
{
	/* FIXME: enable search */

	return asyncns_res_query(asyncns, dname, class, type);
}

int
asyncns_res_done (asyncns_t *asyncns, asyncns_query_t *q, unsigned char **answer)
{
	size_t length;
	const char *query_answer = netresolve_query_get_dns_answer(q->query, &length);

	*answer = malloc(length);
	memcpy(*answer, query_answer, length);

	netresolve_query_free(remove_query(q));

	return 0;
}

asyncns_query_t *
asyncns_getnext (asyncns_t *asyncns)
{
	asyncns_query_t *list = &asyncns->queries;

	for (asyncns_query_t *q = list->next; q != list; q = q->next)
		if (q->done)
			return q;

	return NULL;
}

int
asyncns_getnqueries (asyncns_t *asyncns)
{
	asyncns_query_t *list = &asyncns->queries;
	int count = 0;

	for (asyncns_query_t *q = list->next; q != list; q = q->next)
		count++;

	return count;
}

void
asyncns_cancel (asyncns_t *asyncns, asyncns_query_t *q)
{
	netresolve_query_free(q->query);
	dequeue(q);
	free(q);
}

void
asyncns_free (asyncns_t *asyncns)
{
	asyncns_query_t *list = &asyncns->queries;

	while (list->next != list)
		asyncns_cancel(asyncns, list->next);

	netresolve_context_free(asyncns->context);

	free(asyncns);
}

void
asyncns_freeaddrinfo (struct addrinfo *result)
{
	netresolve_freeaddrinfo(result);
}

void
asyncns_freeanswer (unsigned char *answer)
{
	free(answer);
}

int
asyncns_isdone (asyncns_t *asyncns, asyncns_query_t *q)
{
	return q->done;
}

void
asyncns_setuserdata (asyncns_t *asyncns, asyncns_query_t *q, void *userdata)
{
	q->userdata = userdata;
}

void *
asyncns_getuserdata (asyncns_t *asyncns, asyncns_query_t *q)
{
	return q->userdata;
}
