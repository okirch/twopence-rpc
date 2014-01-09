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
 * Verify svc* functions
 */
#include <netconfig.h>
#include <assert.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <errno.h>
#include "rpctest.h"
#include <rpc/rpc_com.h>

static RPCB		square_regs[] = {
	{ .r_prog = SQUARE_PROG, .r_vers = SQUARE_VERS },
	{ 0 }
};

#define BI(prog, vers, netid, addr) {\
	.r_prog = prog, \
	.r_vers = vers, \
	.r_netid = #netid, \
	.r_addr = addr, \
}
static RPCB		ipv4_reg[] = {
	BI(SQUARE_PROG, SQUARE_VERS, udp, "*"),
	BI(SQUARE_PROG, SQUARE_VERS, tcp, "*"),
	{ 0 }
};
static RPCB		ipv6_reg[] = {
	BI(SQUARE_PROG, SQUARE_VERS, udp6, "*"),
	BI(SQUARE_PROG, SQUARE_VERS, tcp6, "*"),
	{ 0 }
};
static RPCB		local_reg[] = {
	BI(SQUARE_PROG, SQUARE_VERS, local, SQUARE_LOCAL_ADDR),
	{ 0 }
};

static unsigned int	num_transports;
static SVCXPRT *	all_transports[64];

static void	rpctest_verify_svc_register_nettype(const char *);
static void	rpctest_verify_svc_bindings(const RPCB *);
static void	rpctest_svc_free_all(void);
static void	rpctest_check_svc_reachability(const char *, const char *, const RPCB *);
static void	__rpctest_check_svc_reachability(const char *, const RPCB *,
				const char *, const RPCB *);
static RPCB *	rpctest_build_registration_nettype(const char *);
static RPCB *	__rpctest_build_registration_nettype(const char *, const RPCB *);
static const char *concat_netids(const RPCB *);

void
rpctest_verify_svc_register(void)
{
	const char **nettypes, **nt;

	log_test_group("svcreg", "Verify service registration functions");

	unsetenv("NETPATH");

	nettypes = rpctest_get_nettypes();
	for (nt = nettypes; *nt; ++nt)
		rpctest_verify_svc_register_nettype(*nt);

	rpctest_verify_svc_bindings(ipv4_reg);
	rpctest_verify_svc_bindings(ipv6_reg);
	rpctest_verify_svc_bindings(local_reg);

	unlink(SQUARE_LOCAL_ADDR);
}

void
rpctest_verify_svc_register_nettype(const char *nettype)
{
	RPCB *rb, *expect;

	log_test("Create service and register by nettype %s", nettype);

	for (rb = square_regs; rb->r_prog; ++rb) {
		if (!rpctest_register_service_nettype(rb->r_prog, rb->r_vers, square_prog_1, nettype)) {
			log_fail("Failed to register service");
			goto done;
		}
	}

	expect = rpctest_build_registration_nettype(nettype);
	if (expect == NULL) {
		log_fail("Cannot build list of registrations");
		goto done;
	}

	rpctest_verify_rpcb_registration("square program", expect);
	rpctest_check_svc_reachability("square program", nettype, expect);

done:
	rpctest_svc_cleanup(square_regs);

	/* FIXME: verify that the program is no longer registered */

	rpctest_rpcb_unset_wildcard(SQUARE_PROG);
}

void
rpctest_verify_svc_bindings(const RPCB *rblist)
{
	log_test("Create service and register by netid (%s)", concat_netids(rblist));
	if (!rpctest_svc_register_rpcb(rblist, square_prog_1)) {
		log_fail("Failed to register service");
		goto done;
	}

	rpctest_verify_rpcb_registration("square program", rblist);
	rpctest_check_svc_reachability("square program", NULL, rblist);

done:
	rpctest_svc_cleanup(square_regs);

	/* FIXME: verify that the program is no longer registered */

	rpctest_rpcb_unset_wildcard(SQUARE_PROG);
}

int
rpctest_register_service_nettype(unsigned long prog, unsigned long vers, rpc_program_fn_t *progfn, const char *nettype)
{
	if (!svc_create(progfn, prog, vers, nettype)) {
		fprintf (stderr, "unable to register (%lu, %lu, nettype=%s).\n", prog, vers, nettype);
		return 0;
	}

	return 1;
}

int
rpctest_svc_register_rpcb(const RPCB *rpcb, rpc_program_fn_t *dispatch)
{
	const RPCB *rb;
	int success = 1;

	for (rb = rpcb; rb->r_prog; ++rb) {
		struct netconfig *nconf = NULL;
		SVCXPRT *xprt = NULL;

		nconf = getnetconfigent(rb->r_netid);
		if (nconf == NULL)
			log_fatal("Bad netid %s", rb->r_netid);

		if (rb->r_addr == NULL) {
			/* Don't create */
		} else
		if (!strcmp(rb->r_addr, "*")) {
			xprt = svc_tp_create(dispatch, rb->r_prog, rb->r_vers, nconf);
			if (xprt == NULL) {
				log_fail("svc_tp_create(%lu, %lu, %s) failed",
						rb->r_prog, rb->r_vers, rb->r_netid);
				success = 0;
			}
		} else {
			struct netbuf *nb = NULL;
			int fd = -1;

			if (!strcmp(nconf->nc_protofmly, NC_LOOPBACK) && rb->r_addr[0] == '/') {
				if (unlink(rb->r_addr) < 0 && errno != ENOENT) {
					log_fail("Unable to remove stale socket %s: %m", rb->r_addr);
					goto create_failed;
				}
			}

			nb = uaddr2taddr(nconf, rb->r_addr);
			if (nb == NULL) {
				log_fail("Cannot parse %s address %s", rb->r_netid, rb->r_addr);
				goto create_failed;
			}

			fd = rpctest_make_socket(rb->r_netid);
			if (fd < 0) {
				log_fail("Cannot create %s socket: %m", rb->r_netid);
				goto create_failed;
			}

			if (bind(fd, (struct sockaddr *) nb->buf, nb->len) < 0) {
				log_fail("Unable to bind %s socket to address %s: %m", rb->r_netid, rb->r_addr);
				goto create_failed;
			}

			/* If it's a SOCK_STREAM, we want to listen on it */
			(void) listen(fd, 0);

			xprt = svc_tli_create(fd, nconf, NULL, 0, 0);
			if (xprt == NULL) {
				log_fail("svc_tli_create(%s) failed", rb->r_netid);
				goto create_failed;
			}

			if (!svc_reg(xprt, rb->r_prog, rb->r_vers, dispatch, nconf)) {
				log_fail("svc_reg(%lu, %lu, %s, %s) failed",
						rb->r_prog, rb->r_vers, rb->r_netid, rb->r_addr);
				svc_destroy(xprt);
				xprt = NULL;
			}

create_failed:
			if (nb) {
				free(nb->buf);
				free(nb);
			}
			if (xprt == NULL) {
				success = 0;
				close(fd);
			}
		}

		if (xprt)
			all_transports[num_transports++] = xprt;

		freenetconfigent(nconf);
	}

	return success;
}

void
rpctest_svc_cleanup(RPCB *rblist)
{
	const RPCB *rb;

	for (rb = rblist; rb->r_prog; ++rb)
		svc_unreg(rb->r_prog, rb->r_vers);
	rpctest_svc_free_all();
}

void
rpctest_svc_free_all(void)
{
	unsigned int i;

	for (i = 0; i < num_transports; ++i)
		svc_destroy(all_transports[i]);
	num_transports = 0;
}

RPCB *
rpctest_build_registration_nettype(const char *nettype)
{
	return __rpctest_build_registration_nettype(nettype, square_regs);
}

RPCB *
__rpctest_build_registration_nettype(const char *nettype, const RPCB *versions)
{
	static RPCB result[128];
	const char *netids[16];
	unsigned int i, j, numids;
	RPCB *rb;

	memset(result, 0, sizeof(result));

	if (!(numids = rpctest_expand_nettype(nettype, netids, ARRAY_COUNT(netids))))
		log_fatal("could not expand nettype %s", nettype);

	rb = result;
	for (i = 0; versions[i].r_prog; ++i) {
		for (j = 0; j < numids; ++j, ++rb) {
			*rb = versions[i];
			rb->r_netid = (char *) netids[j];
			rb->r_addr = "*";
		}
	}

	return result;
}

void
rpctest_check_svc_reachability(const char *program_name, const char *nettype, const RPCB *bindings)
{
	rpctest_process_t *proc;

	proc = rpctest_fork_server();
	if (proc == NULL)
		log_fatal("%s: unable to fork server process", __FUNCTION__);

	__rpctest_check_svc_reachability(program_name, square_regs, nettype, bindings);

	rpctest_kill_process(proc);
}

void
__rpctest_check_svc_reachability(const char *program_name, const RPCB *version,
				const char *nettype, const RPCB *bindings)
{
	const char *hostname = "localhost";
	CLIENT *clnt;

	if (nettype) {
		const RPCB *vp;

		log_test("Testing reachability of %s via clnt_create(%s)", program_name, nettype);
		for (vp = version; vp->r_prog; ++vp) {
			void *res;

			clnt = clnt_create(hostname, vp->r_prog, vp->r_vers, nettype);
			if (clnt == NULL) {
				log_fail("clnt_create(%s, %lu, %lu, %s) failed: %s",
						hostname, vp->r_prog, vp->r_vers, nettype,
						clnt_spcreateerror("error"));
				return;
			}

			res = rpc_nullproc(clnt);
			if (res == NULL) {
				log_fail("NULL procedure call failed: %s", clnt_sperror(clnt, "error"));
				clnt_destroy(clnt);
				return;
			}

			clnt_destroy(clnt);
		}
	}

	if (bindings) {
		const RPCB *rb;

		log_test("Testing reachability of %s via clnt_tp_create(%s)", program_name,
				concat_netids(bindings));
		for (rb = bindings; rb->r_prog; ++rb) {
			struct netconfig *nconf;
			void *res;

			nconf = getnetconfigent(rb->r_netid);
			if (nconf == NULL)
				log_fatal("Bad netid %s", rb->r_netid);

			clnt = clnt_tp_create_timed(hostname,
					rb->r_prog, rb->r_vers,
					nconf, NULL);
			freenetconfigent(nconf);

			if (clnt == NULL) {
				/* A NULL r_addr means we didn't register the service for
				 * this netid, but wanted to check that it's not there. */
				if (rb->r_addr == NULL)
					continue;

				log_fail("clnt_tp_create(%s, %lu, %lu, %s) failed: %s",
						hostname, rb->r_prog, rb->r_vers, rb->r_netid,
						clnt_spcreateerror("error"));
				return;
			}

			res = rpc_nullproc(clnt);
			if (res == NULL) {
				log_fail("NULL procedure call failed: %s", clnt_sperror(clnt, "error"));
				clnt_destroy(clnt);
				return;
			}

			clnt_destroy(clnt);
		}
	}
}

const char *
concat_netids(const RPCB *rb)
{
	static char buffer[512];
	char *sp;

	memset(buffer, 0, sizeof(buffer));
	for (sp = buffer; rb->r_prog; ++rb) {
		if (rb->r_addr == NULL)
			continue;
		if (sp != buffer)
			*sp++ = '+';
		strcat(sp, rb->r_netid);
		sp += strlen(sp);
	}

	return buffer;
}
