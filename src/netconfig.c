/*
 * RPC Test suite
 *
 * Copyright (C) 2011, Olaf Kirch <okir@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *
 * Verify netconfig functions
 */
#include <netconfig.h>
#include <time.h>
#include <errno.h>
#include "rpctest.h"

#define NC(netid, sem, family, proto) { \
	.nc_netid = #netid, \
	.nc_semantics = NC_TPI_##sem, \
	.nc_protofmly = #family, \
	.nc_proto = #proto, \
}

static struct netconfig	__netconfig_data[] = {
	NC(udp,		CLTS,		inet,		udp),
	NC(tcp,		COTS_ORD,	inet,		tcp),
	NC(udp6,	CLTS,		inet6,		udp),
	NC(tcp6,	COTS_ORD,	inet6,		tcp),
	NC(local,	COTS_ORD,	loopback,	-),
#if 0
	NC(unix,	COTS_ORD,	loopback,	-),
#endif

	{ NULL }
};

struct addr_info {
	const char *	netid;
	const char *	uaddr;
};
static struct addr_info	__uaddr_data[] = {
	{ "udp",	"127.0.0.1.0.1"		},
	{ "tcp",	"127.0.0.1.0.1"		},
	{ "udp6",	"::1.0.1"		},
	{ "tcp6",	"::1.0.1"		},
	{ "local",	"/random/pathname"	},

	{ NULL }
};

static void		rpctest_verify_netconfig(const struct netconfig *);
static const char *	__netconfig_semantics_name(int);
static void		rpctest_verify_sockinfo_all(void);
static void		rpctest_verify_taddr2uaddr(void);

void
rpctest_verify_netconfig_all(void)
{
	unsigned int i;
	void *handle;
	struct netconfig *nconf;

	log_test_group("netconfig", "Verify netconfig functions");

	for (i = 0; __netconfig_data[i].nc_netid; ++i)
		rpctest_verify_netconfig(&__netconfig_data[i]);

	log_test_tagged("builtin", "Verifying configured netids");
	handle = setnetconfig();
	if (handle == NULL) {
		log_fail("setnetconfig failed: %m");
		return;
	}

	while ((nconf = getnetconfig(handle)) != NULL) {
		struct netconfig *check;

		for (check = __netconfig_data; check->nc_netid; ++check) {
			if (!strcmp(check->nc_netid, nconf->nc_netid))
				break;
		}
		if (check->nc_netid)
			continue;

		if (!strcmp(nconf->nc_netid, "unix")
		 || !strcmp(nconf->nc_netid, "rawip"))
			continue;

		log_warn("found unknown netid %s", nconf->nc_netid);
	}
	endnetconfig(handle);

	log_test_group("sockinfo", "Verify sockinfo functions");
	rpctest_verify_sockinfo_all();

	log_test_group("addrconv", "Verify uaddr2taddr/taddr2uaddr functions");
	rpctest_verify_taddr2uaddr();
}

static void
rpctest_verify_netconfig(const struct netconfig *expect)
{
	struct netconfig *nconf;
	unsigned int fail = 0;
	time_t now;

	log_test_tagged(expect->nc_netid, "Verifying netconfig for netid <%s>", expect->nc_netid);

	/* Note, we time the getnetconfigent() call - there's some crappy
	 * old debug code in libtirpc that we should excise */
	now = time(NULL);
	nconf = getnetconfigent(expect->nc_netid);
	now = time(NULL) - now;

	if (nconf == NULL) {
		log_fail("netid <%s> not defined", expect->nc_netid);
		return;
	}

	if (now > 2 && !strcmp(expect->nc_netid, "unix")) {
		log_warn("Looks like libtirpc still has that crappy old warning about the unix transport");
		fail++;
	}

	if (nconf->nc_semantics != expect->nc_semantics) {
		log_warn("Unexpected semantics for netid %s: expected %s, got %s",
				expect->nc_netid,
				__netconfig_semantics_name(expect->nc_semantics),
				__netconfig_semantics_name(nconf->nc_semantics));
		fail++;
	}

	if (strcmp(nconf->nc_protofmly, expect->nc_protofmly)) {
		log_warn("Unexpected protocol family for netid %s: expected %s, got %s",
				expect->nc_netid,
				expect->nc_protofmly,
				nconf->nc_protofmly);
		fail++;
	}

	if (strcmp(nconf->nc_proto, expect->nc_proto)) {
		log_warn("Unexpected protocol for netid %s: expected %s, got %s",
				expect->nc_netid,
				expect->nc_proto,
				nconf->nc_proto);
		fail++;
	}

	freenetconfigent(nconf);

	if (fail)
		log_fail("failed");
}

/*
 * The rpc_sockinfo stuff isn't an exported interface, but it's probably
 * a good idea to check it nevertheless.
 */
#define __SI(__name, af, proto, sotype, addrtype) { \
	.si_netid	= #__name, \
	.si_af		= af, \
	.si_proto	= proto, \
	.si_socktype	= sotype, \
	.si_alen	= sizeof(struct addrtype) \
}
static struct __sockinfo_test {
	const char *	si_netid;
	int		si_af;
	int		si_proto;
	int		si_socktype;
	unsigned int	si_alen;
} __sockinfo_tests[] = {
	__SI(udp,	AF_INET, IPPROTO_UDP, SOCK_DGRAM, sockaddr_in),
	__SI(tcp,	AF_INET, IPPROTO_TCP, SOCK_STREAM, sockaddr_in),
	__SI(udp6,	AF_INET6, IPPROTO_UDP, SOCK_DGRAM, sockaddr_in6),
	__SI(tcp6,	AF_INET6, IPPROTO_TCP, SOCK_STREAM, sockaddr_in6),
	__SI(local,	AF_LOCAL, 0, SOCK_STREAM, sockaddr_un),

	{ NULL }
};

static struct __sockinfo_test *
__rpctest_netid2sockinfo(const char *netid)
{
	struct __sockinfo_test *st;

	for (st = __sockinfo_tests; st->si_netid; ++st) {
		if (!strcmp(st->si_netid, netid))
			return st;
	}
	return NULL;
}

int
rpctest_make_socket(const char *netid)
{
	struct __sockinfo_test *st;

	if (!(st = __rpctest_netid2sockinfo(netid))) {
		errno = EPROTONOSUPPORT;
		return -1;
	}

	return socket(st->si_af, st->si_socktype, st->si_proto);
}

#ifdef __TEST_RPC_SOCKINFO
static void
rpctest_verify_sockinfo_all(void)
{
	struct __sockinfo_test *st;

	for (st = __sockinfo_tests; st->si_netid; ++st) {
		struct netconfig *nconf;
		struct __rpc_sockinfo si;

		log_test_tagged(st->si_netid, "Verifying sockinfo for netid <%s>", st->si_netid);
		nconf = getnetconfigent(st->si_netid);
		if (nconf == NULL) {
			log_fail("Cannot get netconfig for <%s>", st->si_netid);
			continue;
		}

		if (!__rpc_nconf2sockinfo(nconf, &si)) {
			log_fail("Cannot get sockinfo for <%s>", st->si_netid);
			continue;
		}
		freenetconfigent(nconf);

		if (st->si_af != si.si_af
		 || st->si_proto != si.si_proto
		 || st->si_socktype != si.si_socktype
		 || st->si_alen != si.si_alen) {
			log_fail("sockinfo mismatch for netid <%s>", st->si_netid);
			log_warn("Expected af=%d, proto=%d, type=%d, alen=%u",
					st->si_af, st->si_proto,
					st->si_socktype, st->si_alen);
			log_warn("Got af=%d, proto=%d, type=%d, alen=%u",
					si.si_af, si.si_proto,
					si.si_socktype, si.si_alen);
			continue;
		}
	}
}
#else
static void
rpctest_verify_sockinfo_all(void)
{
}
#endif

static void
rpctest_destroy_netbuf(struct netbuf **nbp)
{
	struct netbuf *nb;

	if ((nb = *nbp) == NULL)
		return;

	*nbp = NULL;
	free(nb->buf);
	free(nb);
}

void
rpctest_verify_taddr2uaddr(void)
{
	struct addr_info *aip;
	
	for (aip = __uaddr_data; aip->netid; ++aip) {
		struct netconfig *nconf;
		struct netbuf *nb = NULL;
		char *result = NULL;

		log_test_tagged(aip->netid, "Verifying addr conversion for netid <%s>", aip->netid);
		nconf = getnetconfigent(aip->netid);
		if (nconf == NULL) {
			log_fail("Cannot get netconfig for <%s>", aip->netid);
			goto next;
		}

		nb = uaddr2taddr(nconf, aip->uaddr);
		if (nb == NULL) {
			log_fail("uaddr2taddr(%s, %s): failed", aip->netid, aip->uaddr);
			goto next;
		}

		result = taddr2uaddr(nconf, nb);
		if (result == NULL) {
			log_fail("uaddr2taddr(%s, %s): could not convert back result", aip->netid, aip->uaddr);
			goto next;
		}

		if (strcmp(aip->uaddr, result)) {
			log_fail("uaddr2taddr(%s, %s) not idempotent (returns \"%s\")", aip->netid, aip->uaddr, result);
			goto next;
		}

		/* Pad netbuf */
		nb->buf = realloc(nb->buf, nb->len + 8);
		memset(nb->buf + nb->len, '.', 8);
		nb->maxlen += 8;

		free(result);
		result = taddr2uaddr(nconf, nb);
		if (result == NULL) {
			log_fail("uaddr2taddr(%s, %s): could not convert back padded result", aip->netid, aip->uaddr);
			goto next;
		}

next:
		rpctest_destroy_netbuf(&nb);
		if (result)
			free(result);
	}
}

static const char *
__netconfig_semantics_name(int sem)
{
	switch (sem) {
	case NC_TPI_CLTS:
		return "clts";
	case NC_TPI_COTS:
		return "cots";
	case NC_TPI_COTS_ORD:
		return "cots_ord";
	}

	return "unknown";
}
