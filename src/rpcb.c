/*
 * RPC Test suite
 *
 * Copyright (C) 2011-2012, Olaf Kirch <okir@suse.de>
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
 * Verify rpcbind client functions
 */
#include <netconfig.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <time.h>
#include <netdb.h>
#include <unistd.h>
#include "rpctest.h"

struct rpcb_conninfo {
	const char *	netid;
	const char *	hostname;
	int		af;
	int *		getmaps_crashes_p;
};

extern int	rpctest_rpcb_unset_wildcard(rpcprog_t);
static int	__rpctest_rpcb_unset_wildcard(struct rpcb_conninfo *, rpcprog_t);
extern void	rpctest_verify_rpcb_set(struct rpcb_conninfo *,
				const char *progname, const RPCB *, int, int may_crash);
extern void	rpctest_verify_rpcb_unset(struct rpcb_conninfo *,
				const char *program_name,
				const struct rpcb *, int, int may_crash);
static int	rpctest_verify_rpcb_registration_pmap(struct rpcb_conninfo *,
				const char *, const struct rpcb *);
static void	rpctest_verify_rpcb_getaddr(struct rpcb_conninfo *, const char *, const struct rpcb *);
static int	rpctest_verify_rpcb_gettime(struct rpcb_conninfo *);
static int	rpctest_verify_rpcb_misc(struct rpcb_conninfo *);
static int	rpctest_verify_rpcb_crashme(void);
static int	__rpctest_verify_rpcb_addrfunc(const char *);
static int	__rpctest_verify_rpcb_registration(struct rpcb_conninfo *, const struct rpcb *, int);
static int	__rpctest_verify_rpcb_unregistration(struct rpcb_conninfo *, const struct rpcb *, int);
static RPCB *	__rpctest_rpcb_get_registrations(const struct rpcb_conninfo *, rpcprog_t);
static bool_t	__rpctest_rpcb_set(struct rpcb_conninfo *, const struct rpcb *);
static void	__rpctest_rpcb_unset(struct rpcb_conninfo *, const struct rpcb *);
static CLIENT *	__rpctest_create_client(const char *, const char *, unsigned int, unsigned int,
				char **tgtaddr);
static bool_t	__rpctest_verify_uaddr(const char *netid, const char *uaddr);

#define BI(prog, vers, netid, addr) {\
	.r_prog = prog, \
	.r_vers = vers, \
	.r_netid = #netid, \
	.r_addr = addr, \
}
static struct rpcb	__rpcbind_binding[] = {
	BI(PMAPPROG,	PMAPVERS,	tcp,		"0.0.0.0.0.111"),
	BI(PMAPPROG,	PMAPVERS,	udp,		"0.0.0.0.0.111"),
	BI(RPCBPROG,	RPCBVERS,	tcp,		"0.0.0.0.0.111"),
	BI(RPCBPROG,	RPCBVERS,	udp,		"0.0.0.0.0.111"),
	BI(RPCBPROG,	RPCBVERS,	tcp6,		"::.0.111"),
	BI(RPCBPROG,	RPCBVERS,	udp6,		"::.0.111"),
	BI(RPCBPROG,	RPCBVERS4,	tcp,		"0.0.0.0.0.111"),
	BI(RPCBPROG,	RPCBVERS4,	udp,		"0.0.0.0.0.111"),
	BI(RPCBPROG,	RPCBVERS4,	tcp6,		"::.0.111"),
	BI(RPCBPROG,	RPCBVERS4,	udp6,		"::.0.111"),
	BI(RPCBPROG,	RPCBVERS,	local,		_PATH_RPCBINDSOCK),
	BI(RPCBPROG,	RPCBVERS4,	local,		_PATH_RPCBINDSOCK),
#if 0
	/* One might entertain the idea of using rpcb_getaddr to ask for
	 * a completely outlandish program version, and expect that to fail.
	 * However, that doesn't. The way this is supposed to work is that
	 * we get a valid address, then call that service and get an
	 * RPCVERSMISMATCH error.
	 * I wonder how they couldn't come up with a more complicated
	 * interface :-)
	 */
	BI(RPCBPROG,	666,		local,		NULL),
	BI(RPCBPROG,	666,		tcp,		NULL),
	BI(RPCBPROG,	666,		udp6,		NULL),
#endif
	BI(RPCBPROG,	RPCBVERS,	rawip,		NULL),

	{ 0 }
};

static struct rpcb	__square_binding[] = {
	BI(SQUARE_PROG,	2,		tcp,		"0.0.0.0.8.12"),
	BI(SQUARE_PROG,	2,		udp,		"0.0.0.0.8.12"),
	BI(SQUARE_PROG,	2,		tcp6,		"::.8.12"),
	BI(SQUARE_PROG,	2,		udp6,		"::.8.12"),

	{ 0 }
};

static struct rpcb	__square_binding_root[] = {
	BI(SQUARE_PROG,	2,		tcp,		"0.0.0.0.2.12"),
	BI(SQUARE_PROG,	2,		udp,		"0.0.0.0.2.12"),

	{ 0 }
};

static struct rpcb_conninfo tcp_conn_info = {
	"tcp",
	"localhost"
};

static int		rpcb_getmaps_local_null_crashes;
static int		rpcb_getmaps_local_crashes;

void
rpctest_verify_rpcb_all(unsigned int flags)
{
	static struct rpcb_conninfo conn_info[] = {
		{ "tcp", "localhost", },
		{ "udp", "localhost", },
		{ "tcp6", "localhost", },
		{ "udp6", "localhost", },
		/* This segfaults: */
		{ "local", NULL, .getmaps_crashes_p = &rpcb_getmaps_local_null_crashes },
		{ NULL }
	};
	struct rpcb_conninfo *ci;
	struct ifaddrs *ifa_list;

	log_test_group("rpcbind", "Verify rpcbind client functions");

	ci = &conn_info[0];

	rpctest_verify_rpcb_crashme();

	/*
	 * First bunch of test cases - check bindings advertised by rpcbind
	 */
	rpctest_verify_rpcb_registration("rpcbind", __rpcbind_binding);

	rpctest_rpcb_unset_wildcard(SQUARE_PROG);

	rpctest_verify_rpcb_set(ci, "square program", __square_binding, 0, 0);
	rpctest_verify_rpcb_registration_pmap(ci, "square program", __square_binding);
	rpctest_verify_rpcb_unset(ci, "square program", __square_binding, 0, 0);

	rpctest_rpcb_unset_wildcard(SQUARE_PROG);

	if (flags & RPF_DISPUTED) {
		if (geteuid() == 0) {
			rpctest_verify_rpcb_set(ci, "square program (with privports)", __square_binding_root, 0, 0);
			rpctest_verify_rpcb_unset(ci, "square program (with privports)", __square_binding_root, 0, 0);

			__rpctest_rpcb_unset_wildcard(ci, SQUARE_PROG);

			rpctest_drop_privileges();
			rpctest_verify_rpcb_set(ci, "square program (with privports, unpriv user)", __square_binding_root, 1, 0);
			rpctest_resume_privileges();
		} else {
			rpctest_verify_rpcb_set(ci, "square program (with privports, unpriv user)", __square_binding_root, 1, 0);
		}
	}

	__rpctest_rpcb_unset_wildcard(ci, SQUARE_PROG);
 
	for (ci = conn_info; ci->netid; ++ci) {
		char message[128];

		snprintf(message, sizeof(message), "square program (talking to rpcb via %s transport)", ci->netid);
		rpctest_verify_rpcb_set(ci, message, __square_binding, 0, 0);
		rpctest_verify_rpcb_unset(ci, message, __square_binding, 0, 0);
	}

	rpctest_verify_rpcb_getaddr(conn_info, "square program", __square_binding);

	if (getifaddrs(&ifa_list) < 0) {
		log_fatal("getifaddrs failed: %m");
	} else {
		struct rpcb_conninfo __ci = { NULL };
		char message[128];
		struct ifaddrs *ifp;

		log_test("Verifying that getifaddrs returns proper interface index");
		for (ifp = ifa_list; ifp; ifp = ifp->ifa_next) {
			struct sockaddr *sa = ifp->ifa_addr;

			if (!(ifp->ifa_flags & IFF_UP)
			 || sa == NULL)
				continue;

			if (sa->sa_family == AF_INET6) {
				struct sockaddr_in6 *six = (struct sockaddr_in6 *) sa;

				if (IN6_IS_ADDR_LINKLOCAL(&six->sin6_addr)
				 && six->sin6_scope_id == 0) {
					log_fail("getifaddrs returns link-local address with sin6_scope_id == 0");
					break;
				}
			}
		}

		log_trace("Note: the following may crash on some versions of libtirpc due to a bug in getclienthandle");
		for (ifp = ifa_list; ifp; ifp = ifp->ifa_next) {
			struct sockaddr *sa = ifp->ifa_addr;

			/* sockaddr may be NULL for tunnel interfaces */
			if (!(ifp->ifa_flags & IFF_UP)
			 || sa == NULL)
				continue;

			switch (sa->sa_family) {
			case AF_INET:
				__ci.netid = "tcp";
				break;
			case AF_INET6:
				__ci.netid = "tcp6";
				break;

			default:
				continue;
			}

			__ci.hostname = sockaddr_ntoa(sa, ifp);
			__ci.af = sa->sa_family;

			snprintf(message, sizeof(message), "square program (talking to rpcb at %s)", __ci.hostname);
			rpctest_verify_rpcb_set(&__ci, message, __square_binding, 0, 1);
			rpctest_verify_rpcb_unset(&__ci, message, __square_binding, 0, 1);
		}

		freeifaddrs(ifa_list);
	}

	if (flags & RPF_DISPUTED) {
		rpctest_verify_rpcb_gettime(&tcp_conn_info);
		rpctest_verify_rpcb_misc(&tcp_conn_info);
	}
}

void
rpctest_verify_rpcb_set(struct rpcb_conninfo *conn_info, const char *program_name,
			const struct rpcb *rpcbs, int should_fail, int may_crash)
{
	const struct rpcb *rb;
	int crashed = 0;

	log_test("Verifying rpcb_set operation for %s", program_name);
	for (rb = rpcbs; rb->r_prog; ++rb) {
		struct netconfig *nconf = NULL;
		struct netbuf *addr = NULL;
		bool_t rv;

		nconf = getnetconfigent(rb->r_netid);
		if (!nconf) {
			log_fail("unknown netid %s", rb->r_netid);
			goto next;
		}

		addr = uaddr2taddr(nconf, rb->r_addr);
		if (addr == NULL) {
			log_fail("cannot parse addr <%s> (netid %s)",
					rb->r_addr, rb->r_netid);
			goto next;
		}

		if (may_crash) {
			int termsig;

			switch (rpctest_try_catch_crash(&termsig, &rv)) {
			default:
				/* An error occurred when trying to fork etc. */
				log_fail("fork error");
				return;

			case 0:
				rv = rpcb_set(rb->r_prog, rb->r_vers, nconf, addr);
				exit(rv);

			case 1:
				break;

			case 2:
				log_warn("rpcb_set(%u, %u, %s, %s) CRASHED with signal %u",
						rb->r_prog, rb->r_vers, rb->r_netid, rb->r_addr,
						termsig);
				crashed++;
				continue;
			}
		} else {
			rv = rpcb_set(rb->r_prog, rb->r_vers, nconf, addr);
		}

		if (!rv && !should_fail) {
			log_fail("unable to register %u/%u/%s, addr %s",
					rb->r_prog, rb->r_vers, rb->r_netid, rb->r_addr);
		} else
		if (rv && should_fail) {
			log_fail("succeeded to register %u/%u/%s, addr %s (should have failed)",
					rb->r_prog, rb->r_vers, rb->r_netid, rb->r_addr);
		}

next:
		if (nconf)
			freenetconfigent(nconf);
		if (addr) {
			free(addr->buf);
			free(addr);
		}

	}

	if (crashed) {
		log_trace("Skipping %s verification",
				should_fail? "unregistration" : "registration");
		return;
	}

	if (should_fail) {
		__rpctest_verify_rpcb_unregistration(conn_info, rpcbs, may_crash);
	} else {
		__rpctest_verify_rpcb_registration(conn_info, rpcbs, may_crash);
	}
}

void
rpctest_verify_rpcb_unset(struct rpcb_conninfo *conn_info, const char *program_name,
			const struct rpcb *rpcbs, int should_fail, int may_crash)
{
	log_test("Verifying rpcb_unset operation for %s", program_name);

	/* FIXME: do the crash handling fandango */
	__rpctest_rpcb_unset(conn_info, rpcbs);

	if (!should_fail)
		__rpctest_verify_rpcb_unregistration(conn_info, rpcbs, may_crash);
}

int
rpctest_verify_rpcb_registration(const char *program_name, const struct rpcb *rpcbs)
{
	log_test("Verifying rpcb registration for %s", program_name);
	return __rpctest_verify_rpcb_registration(&tcp_conn_info, rpcbs, 0);
}

/*
 * This procedure tests how rpcbind implements the getaddr() call, in particular,
 * what happens when we use eg an IPv6 transport to query rpcbind for an IPv4
 * registration.
 */
void
rpctest_verify_rpcb_getaddr(struct rpcb_conninfo *conn_info, const char *program_name, const struct rpcb *rpcbs)
{
	struct rpcb_conninfo *ci;

	log_test("Verifying rpcb_getaddr for %s", program_name);
	for (ci = conn_info; ci->netid; ++ci) {
		const struct rpcb *rb;

		for (rb = rpcbs; rb->r_prog; ++rb)
			__rpctest_rpcb_set(ci, rb);
	}

	for (ci = conn_info; ci->netid; ++ci) {
		struct timeval timeout = { 10, 0 };
		const struct rpcb *rb;
		char *rpcb_uaddr = NULL;
		CLIENT *clnt;

		clnt = __rpctest_create_client(ci->netid, ci->hostname, RPCBPROG, RPCBVERS4, &rpcb_uaddr);
		if (clnt == NULL)
			continue;

		for (rb = rpcbs; rb->r_prog; ++rb) {
			char *server_uaddr = NULL;
			RPCB parms = *rb;
			int status;

			if (!strcmp(ci->netid, rb->r_netid))
				parms.r_addr = rpcb_uaddr;
			else
				parms.r_addr = "";
			parms.r_owner = "rpctest";

			status = clnt_call(clnt, RPCBPROC_GETADDR,
						(xdrproc_t) xdr_rpcb, (char *)(void *)&parms,
						(xdrproc_t) xdr_wrapstring, (char *)(void *) &server_uaddr,
						timeout);

			if (status != RPC_SUCCESS) {
				log_fail("getaddr(%s, %u, %u, %s) %s", ci->netid, rb->r_prog, rb->r_vers, rpcb_uaddr,
						clnt_sperror(clnt, "failed"));
				continue;
			} else
			if (strcmp(ci->netid, "local")
			 && !__rpctest_verify_uaddr(ci->netid, server_uaddr)) {
				/* Note: rpcbind ignores the hint in the RPCB struct passed to it, but
			 	 * uses the transport over which the query came in.
				 * Not sure if this is entirely intuitive, but at least it's consistent.
				 */
				log_fail("query for <%s, %u, %u> over %s returns \"%s\", which is not a valid %s address",
						rb->r_netid, rb->r_prog, rb->r_vers, ci->netid,
						printable(server_uaddr),
						ci->netid);
			}

			free(server_uaddr);
		}
		clnt_destroy(clnt);
	}

	__rpctest_rpcb_unset_wildcard(&conn_info[0], SQUARE_PROG);
}

int
__rpctest_verify_rpcb_unregistration(struct rpcb_conninfo *conn_info, const struct rpcb *rpcbs, int may_crash)
{
	static RPCB no_reg[2];
	
	no_reg[0] = rpcbs[0];
	no_reg[0].r_addr = NULL;
	return __rpctest_verify_rpcb_registration(conn_info, no_reg, may_crash);
}

int
__rpctest_verify_rpcb_registration(struct rpcb_conninfo *conn_info, const struct rpcb *rpcbs, int may_crash)
{
	struct netconfig *nconf;
	const char *bind_server;
	const struct rpcb *rb;
	unsigned int i;
	int success = 1;
	int crashed = 0;

	if (rpcbs == NULL || rpcbs[0].r_prog == 0) {
		static int warned;

		if (!warned++)
			log_warn("%s: fix the test program here", __FUNCTION__);
		return 1;
	}

	if ((bind_server = conn_info->hostname) == NULL)
		bind_server = "localhost";

	for (i = 0, rb = rpcbs; rb->r_prog; ++i, ++rb) {
		struct sockaddr_storage ss;
		struct netbuf abuf;
		char *reg_addr;
		int rv;

		abuf.buf = &ss;
		abuf.len = abuf.maxlen = sizeof(ss);

		nconf = getnetconfigent(rb->r_netid);
		if (!nconf)
			log_fatal("no netid %s", rb->r_netid);

		/* By default, we'll be called with symbolic hostnames such as "localhost"
		 * which should expand to something useful for both ipv4 and ipv6.
		 *
		 * However, some tests call here using a conn_info that specifies a numeric
		 * address like 127.0.0.1 or ::1 as the hostname. In this case, we have to
		 * skip transports (ie netids) with a non-matching protocol family.
		 */
		if (conn_info->af != AF_UNSPEC) {
			if ((conn_info->af == AF_INET && strcmp(nconf->nc_protofmly, "inet"))
			 || (conn_info->af == AF_INET6 && strcmp(nconf->nc_protofmly, "inet6"))) {
				freenetconfigent(nconf);
				continue;
			}
		}

		if (may_crash) {
			int termsig;

			switch (rpctest_try_catch_crash(&termsig, &rv)) {
			default:
				/* An error occurred when trying to fork etc. */
				log_fail("fork error");
				return 0;

			case 0:
				// log_trace("rpcb_getaddr(%u, %u, %s, bind=%s)", rb->r_prog, rb->r_vers, rb->r_netid, bind_server);
				rv = rpcb_getaddr(rb->r_prog, rb->r_vers, nconf, &abuf, bind_server);
				exit(rv);

			case 1:
				break;

			case 2:
				log_warn("rpcb_getaddr(%u, %u, %s, bind=%s) CRASHED with signal %u",
						rb->r_prog, rb->r_vers, rb->r_netid, bind_server, termsig);
				crashed++;
				success = 0;
				continue;
			}
		}

		rv = rpcb_getaddr(rb->r_prog, rb->r_vers, nconf, &abuf, bind_server);
		if (!rv) {
			freenetconfigent(nconf);

			/* If we expected this to be registered, this is a bug */
			if (rb->r_addr != NULL) {
				log_fail("%s has no registration for %u/%u/%s (expected %s)",
						conn_info->hostname?: "<localhost>",
						rb->r_prog, rb->r_vers, rb->r_netid,
						rb->r_addr);
				success = 0;
			}
			continue;
		}

		reg_addr = taddr2uaddr(nconf, &abuf);
		freenetconfigent(nconf);

		/*
		log_trace("rpcb_getaddr(%lu, %lu, %s) = %s", rb->r_prog, rb->r_vers, rb->r_netid, reg_addr);
		 */

		if (rb->r_addr == NULL) {
			/* There should be no registration for this one */
			if (reg_addr != NULL && reg_addr[0] != '\0') {
				log_fail("rpcb_getaddr(%u, %u, %s) returns %s, should return error",
						rb->r_prog, rb->r_vers,
						rb->r_netid,
						reg_addr);
				success = 0;
			}
		} else {
			if (reg_addr == NULL || reg_addr[0] == '\0') {
				log_fail("rpcb_getaddr(%lu, %lu, %s) returns bad address",
						rb->r_prog, rb->r_vers, rb->r_netid);
				success = 0;
			} else
			if (!strcmp(rb->r_addr, "*")) {
				/* Special case - we don't know what address we'll register, we
				 * just know something will be registered.
				 *
				 * Do nothing.
				 */
			} else {

#if 0
				/* The following check doesn't work yet. The registered address
				 * will usually use a wildcard (like 0.0.0.0 for IPv4), but getaddr
				 * replaces that with a useful choice - like 127.0.0.1 for loopback.
				 *
				 * In order to test this properly, we'd have to mimim this logic here.
				 */
				if (strcmp(rb->r_addr, reg_addr)) {
					log_fail("Incorrect registration for %u/%u/%s - expected %s, found %s",
							rb->r_prog, rb->r_vers,
							rb->r_netid,
							rb->r_addr, reg_addr);
				}
#endif
			}

		}

		if (reg_addr)
			free(reg_addr);
	}

	if (conn_info->getmaps_crashes_p && *(conn_info->getmaps_crashes_p) != 0)
		return success;

	if (conn_info->hostname != NULL || !rpcb_getmaps_local_null_crashes) {
		RPCB *registered, *reg;

		registered = __rpctest_rpcb_get_registrations(conn_info, rpcbs[0].r_prog);
		if (registered == NULL) {
			log_fail("unable to get registrations from %s via %s",
					conn_info->hostname, conn_info->netid);
			return 0;
		}

		for (reg = registered; reg->r_prog; ++reg) {
			const RPCB *match = NULL;

			for (i = 0, rb = rpcbs; rb->r_prog; ++i, ++rb) {
				if (rb->r_vers == reg->r_vers
				 && !strcmp(rb->r_netid, reg->r_netid)) {
					match = rb;
					break;
				}
			}

			if (match == NULL || match->r_addr == NULL) {
				log_fail("Found unexpected registration %u/%u/%s - %s",
						reg->r_prog, reg->r_vers,
						reg->r_netid,
						reg->r_addr);
				success = 0;
			} else if (strcmp(match->r_addr, "*") && strcmp(match->r_addr, reg->r_addr)) {
				log_fail("Found registration %u/%u/%s with wrong address - found %s, expected %s",
						reg->r_prog, reg->r_vers,
						reg->r_netid,
						reg->r_addr, match->r_addr);
				success = 0;
			}
		}
	}

	return success;
}

int
rpctest_verify_rpcb_registration_pmap(struct rpcb_conninfo *conn_info,
			const char *program_name,
			const struct rpcb *rpcbs)
{
	struct sockaddr_in sin;
	const struct rpcb *rb;
	int success = 1;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	log_test("Verifying portmap visibility of rpcb registration for %s", program_name);
	for (rb = rpcbs; rb->r_prog; ++rb) {
		unsigned long port;

		if (!strcmp(rb->r_netid, "udp"))
			port = pmap_getport(&sin, rb->r_prog, rb->r_vers, IPPROTO_UDP);
		else if (!strcmp(rb->r_netid, "tcp"))
			port = pmap_getport(&sin, rb->r_prog, rb->r_vers, IPPROTO_TCP);
		else
			continue;

		/* log_trace("pmap_getport(%lu, %lu, %s) = %lu", rb->r_prog, rb->r_vers, rb->r_netid, port); */
		if (port == 0) {
			if (rb->r_addr == NULL)
				continue;
			log_fail("pmap_getport(%lu, %lu, %s) failed", rb->r_prog, rb->r_vers, rb->r_netid);
			success = 0;
		} else if (rb->r_addr == NULL) {
			log_fail("pmap_getport(%lu, %lu, %s) returns %lu - should have failed",
					rb->r_prog, rb->r_vers, rb->r_netid, port);
			success = 0;
		} else {
			struct netconfig *nconf;
			struct netbuf *nb;
			uint16_t expect_port;

			nconf = getnetconfigent(rb->r_netid);
			if (!nconf)
				log_fatal("%s: no netid %s", __FUNCTION__, rb->r_netid);
			nb = uaddr2taddr(nconf, rb->r_addr);
			freenetconfigent(nconf);

			if (nb == NULL)
				log_fatal("Cannot parse %s addr %s", rb->r_netid, rb->r_addr);

			expect_port = ntohs(((struct sockaddr_in *) nb->buf)->sin_port);
			free(nb->buf);
			free(nb);

			if (port != expect_port) {
				log_error("pmap_getport(%lu, %lu, %s) returns %lu - expected %u",
					rb->r_prog, rb->r_vers, rb->r_netid, port, expect_port);
				success = 0;
			}
		}
	}

	return success;
}

/*
 * Verify rpcb_gettime
 */
static int
rpctest_verify_rpcb_gettime(struct rpcb_conninfo *conn_info)
{
	int rv, termsig;
	time_t rpcb_time;

	log_test("Verify rpcb_gettime(%s)", conn_info->hostname);
	rpcb_time = 0xd00f00000000ULL;


	switch (rpctest_try_catch_crash(&termsig, &rv)) {
	default:
		/* An error occurred when trying to fork etc. */
		log_fail("fork error");
		return 0;

	case 0:
		rv = rpcb_gettime(conn_info->hostname, &rpcb_time);
		exit(rv);

	case 1:
		break;

	case 2:
		log_warn("rpcb_gettime(%s) CRASHED with signal %u",
				conn_info->hostname,
				termsig);
		return 0;
	}

	if (rv == 0) {
		log_fail("rpcb_gettime failed");
		return 0;
	} else if ((rpcb_time & 0xFFFF00000000) == 0xd00f00000000) {
		log_fail("rpcb_gettime doesn't clear upper bits of time_t");
		return 0;
	} else {
		long delta = rpcb_time - time(NULL);

		if (delta < -1 || 1 < delta) {
			log_fail("rpcb_gettime's time is %lx seconds off", delta);
			return 0;
		}
	}

	return 1;
}

/*
 * Verify rpcbind ancillary functions
 */
static int
rpctest_verify_rpcb_misc(struct rpcb_conninfo *conn_info)
{
	int success = 1;

	if (!__rpctest_verify_rpcb_addrfunc("udp"))
		success = 0;
	if (!__rpctest_verify_rpcb_addrfunc("udp6"))
		success = 0;
	if (!__rpctest_verify_rpcb_addrfunc("tcp"))
		success = 0;
	if (!__rpctest_verify_rpcb_addrfunc("tcp6"))
		success = 0;

	return success;
}

/*
 * Verify that libtirpc handles function calls with bad arguments gracefully
 */
static int
rpctest_verify_rpcb_crashme(void)
{
	static struct crashme_args {
		const char *	netid;
		const char *	hostname;
		int		expect;
		int *		flagp;
	} getmaps_args[] = {
		{ "tcp",	NULL,		RPC_UNKNOWNHOST },
		{ "udp",	NULL,		RPC_UNKNOWNHOST },
		{ "tcp6",	NULL,		RPC_UNKNOWNHOST },
		{ "udp6",	NULL,		RPC_UNKNOWNHOST },
		{ "local",	NULL,		RPC_SUCCESS,		&rpcb_getmaps_local_null_crashes },
		{ "local",	"localhost",	RPC_SUCCESS,		&rpcb_getmaps_local_crashes },
		{ NULL,		"somehost",	RPC_UNKNOWNPROTO },
		{ NULL,		NULL,		RPC_UNKNOWNPROTO },
		{ "-" }
	};
	struct crashme_args *cma;
	struct netbuf *na;
	struct netconfig *nconf = NULL;
	int success = 1;
	int rv;

	for (cma = getmaps_args; ; ++cma) {
		rpcblist *rbl;
		int termsig;

		if (cma->netid && cma->netid[0] == '-')
			break;
		log_test("Trying rpcb_getmaps(%s, %s)",
				cma->netid ?: "NULL",
				cma->hostname ?: "NULL");

		nconf = NULL;
		if (cma->netid) {
			nconf = getnetconfigent(cma->netid);
			if (nconf == NULL) {
				log_fail("Unknown netid");
				success = 0;
				continue;
			}
		}

		switch (rpctest_try_catch_crash(&termsig, &rv)) {
		default:
			/* An error occurred when trying to fork etc. */
			return 0;

		case 0:
			(void) rpcb_getmaps(nconf, cma->hostname);
			exit(0);

		case 1:
			break;

		case 2:
			if (!cma->netid || !cma->hostname)
				log_warn("rpc_getmaps CRASHED with signal %u", termsig);
			else
				log_fail("rpc_getmaps CRASHED with signal %u", termsig);
			if (cma->flagp)
				*cma->flagp = 1;
			continue;
		}

		rbl = rpcb_getmaps(nconf, cma->hostname);
		if (!rpctest_verify_status(rbl == NULL, NULL, cma->expect))
			success = 0;
		if (rbl != NULL)
			xdr_free((xdrproc_t) xdr_rpcblist, &rbl);

		log_test("Trying rpcb_getaddr(%s, %s)",
				cma->netid ?: "NULL",
				cma->hostname ?: "NULL");

		na = rpctest_get_static_netbuf(128);
		rv = rpcb_getaddr(100000, 2, nconf, na, cma->hostname);
		if (!rpctest_verify_status(!rv, NULL, cma->expect))
			success = 0;

		if (nconf)
			freenetconfigent(nconf);
	}

	nconf = getnetconfigent("udp");

	/* Passing in a netbuf with not enough room to store the
	 * server address should fail. */
	na = rpctest_get_static_netbuf(2);
	rv = rpcb_getaddr(100000, 2, nconf, na, "localhost");
	if (!rpctest_verify_status(!rv, NULL, RPC_FAILED))
		success = 0;

	na = rpctest_get_static_netbuf_canary(sizeof(struct sockaddr_in), 8);
	rv = rpcb_getaddr(100000, 2, nconf, na, "localhost");
	if (!rpctest_verify_status(!rv, NULL, RPC_SUCCESS))
		success = 0;
	else if (!rpctest_verify_netbuf_canary(na, 8)) {
		log_fail("rpcb_getaddr wrote past end of buffer");
		success = 0;
	}

	freenetconfigent(nconf);

	return success;
}

/*
 * Verify rpcb_uaddr2taddr and vice versa.
 * The current code in libtirpc (as of 2011-02-13) fails because it
 * always uses the AF_LOCAL transport to talk to rpcbind. Which means
 * rpcbind will try to interpret the passed addresses as AF_LOCAL addrs.
 *
 * The code should really use the transport specified by nconf.
 *
 * However, I don't think this is really worth it. These functions are
 * clearly marked for kernel use only (in case it ever wants to mess
 * with TIRPC netbufs, that is).
 */
static int
__rpctest_verify_rpcb_addrfunc(const char *netid)
{
	struct netconfig *nconf = NULL;
	struct netbuf addrbuf, *result = NULL;
	struct sockaddr_storage input;
	char *uaddr = NULL, *expect_uaddr = NULL;
	int success = 1;

	nconf = getnetconfigent(netid);
	if (!nconf)
		log_fatal("bad netid %s", netid);

	memset(&input, 0, sizeof(input));
	addrbuf.buf = &input;
	addrbuf.len = addrbuf.maxlen = sizeof(input);

	if (!strcmp(nconf->nc_protofmly, "inet")) {
		struct sockaddr_in *sin = (struct sockaddr_in *) &input;

		sin->sin_family = AF_INET;
		sin->sin_port = htons(111);
		sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addrbuf.len = sizeof(*sin);
	} else if (!strcmp(nconf->nc_protofmly, "inet6")) {
		struct sockaddr_in6 *six = (struct sockaddr_in6 *) &input;

		six->sin6_family = AF_INET6;
		six->sin6_port = htons(111);
		six->sin6_addr = in6addr_loopback;
		addrbuf.len = sizeof(*six);
	} else {
		return 0;
	}

	log_test("Trying rpcb_taddr2uaddr(%s, %s)", netid,
			sockaddr_ntoa((struct sockaddr *) &input, NULL));

	expect_uaddr = taddr2uaddr(nconf, &addrbuf);
	if (expect_uaddr == NULL) {
		log_fail("local taddr2uaddr(%s, %s) failed",
				netid, sockaddr_ntoa((struct sockaddr *) &input, NULL));
		goto failed;
	}

	uaddr = rpcb_taddr2uaddr(nconf, &addrbuf);
	if (uaddr == NULL) {
		log_fail("rpcb_taddr2uaddr call failed");
		success = 0;
	} else {
		if (*uaddr == '\0') {
			log_fail("rpcb_taddr2uaddr: server was unable to translate address");
			success = 0;
		} else if (strcmp(uaddr, expect_uaddr)) {
			log_fail("rpcb_taddr2uaddr: server returned %s, expected %s",
					uaddr, expect_uaddr);
			success = 0;
		}
	}


	log_test("Trying rpcb_uaddr2taddr(%s, %s)", netid, expect_uaddr);
	result = rpcb_uaddr2taddr(nconf, expect_uaddr);
	if (result == NULL) {
		log_fail("rpcb_uaddr2taddr failed");
		goto failed;
	}

	if (result->len == 0) {
		log_fail("rpcb_uaddr2taddr: server was unable to translate address");
		goto failed;
	}
	if (result->len != addrbuf.len) {
		log_fail("rpcb_uaddr2taddr returned bad address length %u (expect %u)",
				result->len, addrbuf.len);

#if 0
		{
			unsigned int i;

			printf("result:");
			for (i = 0; i < result->len; ++i)
				printf(" %02x", ((unsigned char *) result->buf)[i]);
			printf("\n");
			fflush(stdout);
		}
#endif

		if (!memcmp(result->buf, addrbuf.buf, addrbuf.len)) {
			log_fail("... but result matches original taddr!");
			goto failed;
		}
		goto failed;
	}
	if (memcmp(result->buf, addrbuf.buf, addrbuf.len)) {
		log_fail("rpcb_uaddr2taddr returned bad address (%s)",
				sockaddr_ntoa((struct sockaddr *) result->buf, NULL));
		goto failed;
	}

done:
	freenetconfigent(nconf);
	if (uaddr)
		free(uaddr);
	if (expect_uaddr)
		free(expect_uaddr);
	if (result) {
		free(result->buf);
		free(result);
	}
	return success;

failed:
	success = 0;
	goto done;
}

/*
 * Verify whether a given uaddr is valid for a given netid
 */
bool_t
__rpctest_verify_uaddr(const char *netid, const char *uaddr)
{
	struct netconfig *nconf = NULL;
	struct __rpc_sockinfo si;
	struct netbuf *nb;
	bool_t rv = 0;

	if (!(nconf = getnetconfigent(netid)))
		return 0;

	if (!__rpc_nconf2sockinfo(nconf, &si))
		goto out;

	nb = uaddr2taddr(nconf, uaddr);
	if (nb == NULL)
		goto out;

	free(nb->buf);
	free(nb);
	rv = 1;

out:
	if (nconf)
		freenetconfigent(nconf);
	return rv;
}


CLIENT *
__rpctest_create_client(const char *netid, const char *hostname, unsigned int prog, unsigned int vers,
				char **tgtaddr)
{
	struct netbuf taddr;
	struct netconfig *nconf;
	struct __rpc_sockinfo si;
	CLIENT *clnt = NULL;
	int sock = -1;

	if (!(nconf = getnetconfigent(netid))) {
		log_fail("unknown netid \"%s\"", netid);
		goto out;
	}

	if (!__rpc_nconf2sockinfo(nconf, &si)) {
		log_fail("no socket info for netid %s", netid);
		goto out;
	}

	sock = socket(si.si_af, si.si_socktype, si.si_proto);
	if (sock < 0) {
		log_fail("cannot create socket for netid %s: %m", netid);
		goto out;
	}

	if (si.si_af == AF_LOCAL) {
		struct sockaddr_un sun;
		unsigned int tsize;

		if (prog == RPCBPROG && hostname == NULL)
			hostname = _PATH_RPCBINDSOCK;
		if (hostname == NULL) {
			log_fail("%s: NULL hostname", __func__);
			goto out;
		}

		memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_LOCAL;
		strncpy(sun.sun_path, hostname, sizeof(sun.sun_path));

		taddr.buf = &sun;
		taddr.len = SUN_LEN(&sun);
		taddr.maxlen = sizeof(sun);

		tsize = __rpc_get_t_size(si.si_af, si.si_proto, 0);

		clnt = clnt_vc_create(sock, &taddr, prog, vers, tsize, tsize);
	} else {
		struct addrinfo hints, *res, *tres;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = si.si_af;
		hints.ai_socktype = si.si_socktype;
		hints.ai_protocol = si.si_proto;
		hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;

		if (getaddrinfo(hostname, "sunrpc", &hints, &res) != 0) {
			log_fail("unable to resolve hostname \"%s\"", hostname);
			goto out;
		}

		for (tres = res; tres != NULL && !clnt; tres = tres->ai_next) {
			taddr.buf = tres->ai_addr;
			taddr.len = taddr.maxlen = tres->ai_addrlen;

			clnt = clnt_tli_create(sock, nconf, &taddr, prog, vers, 0, 0);
		}

		freeaddrinfo(res);
	}

	if (clnt == NULL) {
		log_fail("unable to create client for netid %s", netid);
	} else {
		*tgtaddr = taddr2uaddr(nconf, &taddr);
	}

out:
	if (nconf)
		freenetconfigent(nconf);
	if (sock >= 0 && !clnt)
		close(sock);
	return clnt;
}

CLIENT *
rpctest_rpcb_client(const char *netid, const char *hostname, unsigned int vers, char **tgtaddr)
{
	return __rpctest_create_client(netid, hostname, RPCBPROG, vers, tgtaddr);
}

/*
 * Unset all versions of a given RPC program
 */
int
rpctest_rpcb_unset_wildcard(rpcprog_t prog)
{
	return __rpctest_rpcb_unset_wildcard(&tcp_conn_info, prog);
}

int
__rpctest_rpcb_unset_wildcard(struct rpcb_conninfo *conn_info, rpcprog_t prog)
{
	struct rpcb *registered;

	registered = __rpctest_rpcb_get_registrations(conn_info, prog);
	if (registered == NULL) {
		log_fail("unable to get registrations from %s via %s",
				conn_info->hostname, conn_info->netid);
		return 0;
	}

	__rpctest_rpcb_unset(conn_info, registered);
	return 1;
}

RPCB *
rpctest_rpcb_get_registrations(rpcprog_t match_prog)
{
	return __rpctest_rpcb_get_registrations(&tcp_conn_info, match_prog);
}

RPCB *
__rpctest_rpcb_get_registrations(const struct rpcb_conninfo *conn_info, rpcprog_t match_prog)
{
	static RPCB result[128];
	struct netconfig *nconf;
	rpcblist *rbl, *next;
	unsigned int i = 0;

	memset(result, 0, sizeof(result));

	nconf = getnetconfigent(conn_info->netid);
	if (!nconf)
		log_fatal("no netid %s", conn_info->netid);

	rbl = rpcb_getmaps(nconf, conn_info->hostname);
	freenetconfigent(nconf);

	if (rbl == NULL) {
		log_error("rpcb_getmaps(%s, %s) failed", conn_info->netid, conn_info->hostname);
		return NULL;
	}

	for (; rbl && ((next = rbl->rpcb_next) || 1); rbl = next) {
		RPCB *reg = &rbl->rpcb_map;

		if (match_prog == 0 || reg->r_prog == match_prog) {
			if (i > 127) {
				log_error("%s: too many registrations", __FUNCTION__);
				return NULL;
			}
			result[i++] = *reg;
		}

		free(rbl);
	}

	return result;
}

bool_t
__rpctest_rpcb_set(struct rpcb_conninfo *conn_info, const struct rpcb *rb)
{
	struct netconfig *nconf = NULL;
	struct netbuf *addr = NULL;
	bool_t rv;

	nconf = getnetconfigent(rb->r_netid);
	if (!nconf) {
		log_fail("unknown netid %s", rb->r_netid);
		return 0;
	}

	addr = uaddr2taddr(nconf, rb->r_addr);
	if (addr == NULL) {
		log_fail("cannot parse addr <%s> (netid %s)",
				rb->r_addr, rb->r_netid);
		return 0;
	}

	freenetconfigent(nconf);
	nconf = getnetconfigent(conn_info->netid);

	rv = rpcb_set(rb->r_prog, rb->r_vers, nconf, addr);

	if (nconf)
		freenetconfigent(nconf);
	if (addr) {
		free(addr->buf);
		free(addr);
	}

	return rv;
}

void
__rpctest_rpcb_unset(struct rpcb_conninfo *conn_info, const struct rpcb *rpcbs)
{
	const struct rpcb *rb;

	for (rb = rpcbs; rb->r_prog; ++rb) {
		struct netconfig *nconf;

		nconf = getnetconfigent(rb->r_netid);
		if (!nconf) {
			log_fail("unknown netid %s", rb->r_netid);
			continue;
		}

		if (!rpcb_unset(rb->r_prog, rb->r_vers, nconf))
			log_fail("rpcb_unset(%lu, %lu, %s) failed", rb->r_prog, rb->r_vers, rb->r_netid);
		freenetconfigent(nconf);
	}
}
