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
 * Verify clnt* functions
 */
#include <netconfig.h>
#include <assert.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include "rpctest.h"
#include <rpc/rpc_com.h>

#define BI(prog, vers, netid, addr) {\
	.r_prog = prog, \
	.r_vers = vers, \
	.r_netid = #netid, \
	.r_addr = addr, \
}
static RPCB		all_regs[] = {
	BI(SQUARE_PROG, SQUARE_VERS, udp, "*"),
	BI(SQUARE_PROG, SQUARE_VERS, tcp, "*"),
	BI(SQUARE_PROG, SQUARE_VERS, udp6, "*"),
	BI(SQUARE_PROG, SQUARE_VERS, tcp6, "*"),
	BI(SQUARE_PROG, SQUARE_VERS, local, SQUARE_LOCAL_ADDR),
	{ 0 }
};

#define __BAD_NETTYPE	"frankzappa"
#define __ABC		"abcdefghijklmnopqrstuvwxyz012345678"
#define __DUP(_s)	_s _s
#define __HUGE_NETTYPE	__DUP(__DUP(__ABC))
#define __HUGE_HOSTNAME	__DUP(__DUP(__DUP(__DUP(__DUP(__DUP(__ABC ".")))))) /* 1024 bytes */

static int	rpctest_verify_clnt_create_vers(rpcprog_t, rpcvers_t, rpcvers_t);
static int	rpctest_verify_clnttcp_create(const struct sockaddr *, rpcprog_t, rpcvers_t);
static int	rpctest_verify_clntudp_create(const struct sockaddr *, rpcprog_t, rpcvers_t);
static int	rpctest_verify_clntunix_create(rpcprog_t, rpcvers_t);
static int	rpctest_verify_tcp_sendsz(const struct sockaddr *, rpcprog_t, rpcvers_t);
static int	rpctest_verify_callrpc(const char *, rpcprog_t, rpcvers_t);
static int	rpctest_verify_call_errors(const struct sockaddr *, rpcprog_t, rpcvers_t);
static int	rpctest_verify_clnt_create_fail(const char *hostname,
			rpcprog_t prog, rpcvers_t vers, const char *how,
			const char *nettype, enum clnt_stat expect_status);
static int	rpctest_verify_getrpcport(rpcprog_t, rpcvers_t, int);

void
rpctest_verify_clnt_funcs(void)
{
	rpctest_process_t *proc = NULL;
	const struct sockaddr *srv_addr;
	struct sockaddr_in my_addr;

	log_test_group("client", "Verify client functions");

	unsetenv("NETPATH");

	log_test("Create service and register all transports");
	if (!rpctest_svc_register_rpcb(all_regs, square_prog_1)) {
		log_fail("Failed to register service");
		goto done;
	}

	proc = rpctest_fork_server();
	if (proc == NULL)
		log_fatal("%s: unable to fork server process", __FUNCTION__);

	rpctest_verify_clnt_create_vers(SQUARE_PROG, SQUARE_VERS, SQUARE_VERS);

	srv_addr = loopback_address(AF_INET);
	rpctest_verify_clnttcp_create(srv_addr, SQUARE_PROG, SQUARE_VERS);
	rpctest_verify_clntudp_create(srv_addr, SQUARE_PROG, SQUARE_VERS);
	rpctest_verify_clntunix_create(SQUARE_PROG, SQUARE_VERS);

	rpctest_verify_callrpc("localhost", SQUARE_PROG, SQUARE_VERS);
	rpctest_verify_call_errors(srv_addr, SQUARE_PROG, SQUARE_VERS);

	log_test("Verifying get_myaddress");
	get_myaddress(&my_addr);
	if (my_addr.sin_port != htons(PMAPPORT))
		log_fail("get_myaddress returns port %u (expected %u)",
				ntohs(my_addr.sin_port), PMAPPORT);

	my_addr.sin_port = 0;
	if (memcmp(srv_addr, &my_addr, sizeof(my_addr))) {
		rpctest_verify_clnttcp_create((struct sockaddr *) &my_addr, SQUARE_PROG, SQUARE_VERS);
		rpctest_verify_clntudp_create((struct sockaddr *) &my_addr, SQUARE_PROG, SQUARE_VERS);
	}

	log_test("Verify getrpcport()");
	rpctest_verify_getrpcport(SQUARE_PROG, SQUARE_VERS, IPPROTO_UDP);
	rpctest_verify_getrpcport(SQUARE_PROG, SQUARE_VERS, IPPROTO_TCP);

	rpctest_verify_tcp_sendsz(srv_addr, SQUARE_PROG, SQUARE_VERS);

	rpctest_verify_clnt_create_fail("localhost", SQUARE_PROG, SQUARE_VERS, "bad nettype", __BAD_NETTYPE, RPC_UNKNOWNPROTO);
	rpctest_verify_clnt_create_fail("localhost", SQUARE_PROG, SQUARE_VERS, "huge nettype", __HUGE_NETTYPE, RPC_UNKNOWNPROTO);

#if 0
	if (!rpctest_verify_clnt_create_fail("localhost", SQUARE_PROG, 5, "bad version", "tcp", RPC_UNKNOWNPROTO))
		fprintf(stderr,
			"    Editor's comment on the above failure:\n"
			"      This is due to a design problem in libtirpc/rpcbind. If there is no registration \n"
			"      with an exact version match, rpcbind will return a random binding for that program and\n"
			"      leave it to the server to return a VERSMISMATCH when the client does its first procedure\n"
			"      call. Needless to say, this is rather unintuitive\n");
#endif

	setenv("NETPATH", "udp6", 1);
	rpctest_verify_clnt_create_fail("127.0.0.1", SQUARE_PROG, SQUARE_VERS, "IPv4 address and IPv6 netid", "netpath", RPC_UNKNOWNHOST);
	setenv("NETPATH", "udp", 1);
	rpctest_verify_clnt_create_fail("::1", SQUARE_PROG, SQUARE_VERS, "IPv6 address and IPv4 netid", "netpath", RPC_UNKNOWNHOST);
	unsetenv("NETPATH");
	rpctest_verify_clnt_create_fail(__HUGE_HOSTNAME, SQUARE_PROG, SQUARE_VERS, "huge hostname", "netpath", RPC_UNKNOWNHOST);

done:
	if (proc)
		rpctest_kill_process(proc);
	rpctest_svc_cleanup(all_regs);
	rpctest_rpcb_unset_wildcard(SQUARE_PROG);

	unlink(SQUARE_LOCAL_ADDR);
}

/*
 * Verify client handle by performing a NULL procedure call
 */
static int
rpctest_verify_clnt_handle(CLIENT *clnt)
{
	void *res;

	res = rpc_nullproc(clnt);
	if (res == NULL) {
		log_fail("NULL procedure call failed: %s", clnt_sperror(clnt, "error"));
		clnt_destroy(clnt);
		return 0;
	}

	clnt_destroy(clnt);
	return 1;
}

int
rpctest_verify_clnt_create_vers(rpcprog_t prog, rpcvers_t min_vers, rpcvers_t max_vers)
{
	const char **nettypes, **nt;
	int success = 1;

	nettypes = rpctest_get_nettypes();
	for (nt = nettypes; *nt; ++nt) {
		const char *nettype = *nt;
		rpcvers_t chosen_vers;
		CLIENT	*clnt;

		log_test("Verify clnt_create_vers(%s)", nettype);
		clnt = clnt_create_vers("localhost", prog,
				&chosen_vers, max_vers + 1, max_vers + 2, nettype);
		if (clnt != NULL) {
			log_fail("clnt_create_vers(%lu, min=%lu, max=%lu, %s) succeeded (versions out of range)",
					prog, max_vers + 1, max_vers + 2, nettype);
			success = 0;
			clnt_destroy(clnt);
		} else if (rpc_createerr.cf_stat != RPC_PROGVERSMISMATCH) {
			log_fail("clnt_create_vers returned error %u (expected %u)",
					rpc_createerr.cf_stat,
					RPC_RPCBFAILURE);
			success = 0;
		}

		clnt = clnt_create_vers("localhost", prog,
				&chosen_vers, min_vers, max_vers + 1, nettype);

		if (clnt == NULL) {
			log_fail("clnt_create_vers(%lu, min=%lu, max=%lu, %s) failed",
					prog, min_vers, max_vers, nettype);
			success = 0;
			continue;
		}

		if (chosen_vers < min_vers || max_vers < chosen_vers) {
			log_fail("clnt_create_vers selected version %lu (not in range)", chosen_vers);
			success = 0;
		}

		if (!rpctest_verify_clnt_handle(clnt))
			success = 0;
	}

	return success;
}

int
rpctest_verify_clnt_create_fail(const char *hostname,
		rpcprog_t prog, rpcvers_t vers, const char *how,
		const char *nettype, enum clnt_stat expect_status)
{
	CLIENT *clnt;
	int rv;

	log_test("Verify clnt_create with %s", how);
	clnt = clnt_create(hostname, prog, vers, nettype);

	rv = rpctest_verify_status(clnt == NULL, NULL, expect_status);
	if (clnt != NULL)
		rv = rpctest_verify_clnt_handle(clnt) && rv;

	return rv;
}

int
rpctest_verify_clnttcp_create(const struct sockaddr *srv_addr, rpcprog_t prog, rpcvers_t vers)
{
	struct sockaddr_in bound_addr = *(const struct sockaddr_in *) srv_addr;
	int sockfd = RPC_ANYSOCK;
	CLIENT	*clnt;

	log_test("Verify clnttcp_create(%s, %lu, %lu)", sockaddr_ntoa(srv_addr, NULL), prog, vers);
	clnt = clnttcp_create(&bound_addr, prog, vers, &sockfd, 0, 0);
	if (clnt == NULL) {
		log_fail("clnttcp_create(%lu, %lu) failed", prog, vers);
		return 0;
	}
	if (bound_addr.sin_port == 0)
		log_fail("clnttcp_create did not update port of server address");
	else if (geteuid() == 0 && ntohs(bound_addr.sin_port >= IPPORT_RESERVED))
		log_warn("clnttcp_create bound to non-privileged port while running as root");

	return rpctest_verify_clnt_handle(clnt);
}

int
rpctest_verify_clntudp_create(const struct sockaddr *srv_addr, rpcprog_t prog, rpcvers_t vers)
{
	struct sockaddr_in bound_addr = *(const struct sockaddr_in *) srv_addr;
	struct timeval wait = { 5, 0 };
	int sockfd = RPC_ANYSOCK;
	CLIENT	*clnt;

	log_test("Verify clntudp_create(%s, %lu, %lu)", sockaddr_ntoa(srv_addr, NULL), prog, vers);
	clnt = clntudp_create(&bound_addr, prog, vers, wait, &sockfd);
	if (clnt == NULL) {
		log_fail("clntudp_create(%lu, %lu) failed", prog, vers);
		return 0;
	}
	if (bound_addr.sin_port == 0)
		log_fail("clntudp_create did not update port of server address");
	else if (geteuid() == 0 && ntohs(bound_addr.sin_port >= IPPORT_RESERVED))
		log_warn("clntudp_create bound to non-privileged port while running as root");

	return rpctest_verify_clnt_handle(clnt);
}

int
rpctest_verify_clntunix_create(rpcprog_t prog, rpcvers_t vers)
{
	int sockfd = RPC_ANYSOCK;
	CLIENT	*clnt;
	int termsig, rv;

	log_test("Verify clntunix_create()");

	switch (rpctest_try_catch_crash(&termsig, &rv)) {
	default:
		/* An error occurred when trying to fork etc. */
		return 0;

	case 0:
		clnt = clntunix_create((struct sockaddr_un *) build_local_address(SQUARE_LOCAL_ADDR),
			prog, vers, &sockfd, 0, 0);
		exit(0);

	case 1:
		break;

	case 2:
		log_fail("clntunix_create(%s, %lu, %lu) CRASHED with signal %u",
				SQUARE_LOCAL_ADDR, prog, vers,
				termsig);
		return 0;
	}

	clnt = clntunix_create((struct sockaddr_un *) build_local_address(SQUARE_LOCAL_ADDR),
			prog, vers, &sockfd, 0, 0);
	if (clnt == NULL) {
		log_fail("clntunix_create(%s, %lu, %lu) failed", SQUARE_LOCAL_ADDR, prog, vers);
		return 0;
	}

	return rpctest_verify_clnt_handle(clnt);
}

int
rpctest_verify_callrpc(const char *localhost, rpcprog_t prog, rpcvers_t vers)
{
	enum clnt_stat rv;

	log_test("Verify callrpc(%s, %lu, %lu)", localhost, prog, vers);
	rv = callrpc(localhost, prog, vers, 0,
			(xdrproc_t) xdr_void, NULL,
			(xdrproc_t) xdr_void, NULL);
	if (rv != RPC_SUCCESS) {
		log_fail("callrpc returns error %d (%s)", rv, clnt_sperrno(rv));
		return 0;
	}

	return 1;
}

/*
 * Verify RPC error propagation
 */
static bool_t
xdr_fail(XDR *x, void *p)
{
	return FALSE;
}

static int
rpctest_expect_error(CLIENT *clnt, rpcproc_t proc, enum clnt_stat expect_stat,
			unsigned int ad1, unsigned int ad2)
{
	struct timeval wait = { 25, 0 };
	xdrproc_t encode, decode;
	enum clnt_stat rv;

	log_test("Verify RPC error reporting for error %u (%s)", expect_stat, clnt_sperrno(expect_stat));
	encode = decode = (xdrproc_t) xdr_void;
	if (expect_stat == RPC_CANTENCODEARGS)
		encode = (xdrproc_t) xdr_fail;
	else if (expect_stat == RPC_CANTDECODERES)
		decode = (xdrproc_t) xdr_fail;

	rv = clnt_call (clnt, proc, encode, NULL, decode, NULL, wait);
	if (rv != expect_stat) {
		log_fail("Expected status %s, received %s",
				clnt_sperrno(expect_stat),
				clnt_sperrno(rv));
		return 0;
	} else if (rv == RPC_PROGVERSMISMATCH) {
		struct rpc_err rpcerr;

		clnt_geterr(clnt, &rpcerr);
		if (rpcerr.re_vers.low != ad1
		 || rpcerr.re_vers.high != ad2) {
			log_fail("RPC_PROGVERSMISMATCH: expected min=%u, max=%u; got min=%u, max=%u",
					ad1, ad2, rpcerr.re_vers.low, rpcerr.re_vers.high);
			return 0;
		}
	} else if (rv == RPC_AUTHERROR) {
		struct rpc_err rpcerr;

		clnt_geterr(clnt, &rpcerr);
		if (rpcerr.re_why != ad1) {
			log_fail("RPC_AUTHERROR: expected why=%u, got why=%u", ad1, rpcerr.re_why);
			return 0;
		}
	}

	return 1;
}

int
rpctest_verify_call_errors(const struct sockaddr *srv_addr, rpcprog_t prog, rpcvers_t vers)
{
	struct sockaddr_in bound_addr = *(const struct sockaddr_in *) srv_addr;
	struct timeval wait = { 5, 0 };
	int sockfd = RPC_ANYSOCK;
	CLIENT	*clnt;
	int success = 1;

	clnt = clntudp_create(&bound_addr, prog, vers, wait, &sockfd);
	if (clnt == NULL) {
		log_fail("clntudp_create(%lu, %lu) failed", prog, vers);
		return 0;
	}

	success = rpctest_expect_error(clnt, ERRNOPROG, RPC_PROGUNAVAIL, 0, 0) && success;
	success = rpctest_expect_error(clnt, ERRPROGVERS, RPC_PROGVERSMISMATCH, 2, 5) && success;
	success = rpctest_expect_error(clnt, ERRNOPROC, RPC_PROCUNAVAIL, 0, 0) && success;
	success = rpctest_expect_error(clnt, ERRDECODE, RPC_CANTDECODEARGS, 0, 0) && success;
	success = rpctest_expect_error(clnt, ERRSYSTEMERR, RPC_SYSTEMERROR, 0, 0) && success;
	success = rpctest_expect_error(clnt, ERRWEAKAUTH, RPC_AUTHERROR, AUTH_TOOWEAK, 0) && success;
	success = rpctest_expect_error(clnt, 0, RPC_CANTENCODEARGS, 0, 0) && success;
	success = rpctest_expect_error(clnt, 0, RPC_CANTDECODERES, 0, 0) && success;

	clnt_destroy(clnt);
	return success;
}

/*
 * Verify impact of TCP sendsize/recvsize on correctness.
 * Small send size triggers generation of smaller records.
 */
int
rpctest_verify_tcp_sendsz(const struct sockaddr *srv_addr, rpcprog_t prog, rpcvers_t vers)
{
	static unsigned int sendsize[] = { 4, 8, 12, 16, 31, 64, 77, 128, 256, 1024, 4096, 32 * 1024, 0};
	struct sockaddr_in bound_addr = *(const struct sockaddr_in *) srv_addr;
	static unsigned int buffer[16 * 1024];
	struct foodata foodata;
	unsigned int i, *ssp, expect_sum;
	int success = 1;

	srandom(0xdeadbeef);
	foodata.buffer.buffer_val = buffer;
	foodata.buffer.buffer_len = ARRAY_COUNT(buffer);
	for (i = 0, expect_sum = 0; i < foodata.buffer.buffer_len; ++i)
		expect_sum += buffer[i] = random();

	for (ssp = sendsize; *ssp; ++ssp) {
		struct timeval wait = { 25, 0 };
		int sockfd = RPC_ANYSOCK;
		CLIENT	*clnt;
		enum clnt_stat rv;
		unsigned int sum;

		log_test("Verify TCP client with send size %u", *ssp);

		clnt = clnttcp_create(&bound_addr, prog, vers, &sockfd, *ssp, 0);
		if (clnt == NULL) {
			log_fail("clnttcp_create() failed: %s", prog, vers,
					clnt_spcreateerror("error"));
			success = 0;
			continue;
		}

		rv = clnt_call(clnt, SUMPROC,
				(xdrproc_t) xdr_foodata, &foodata,
				(xdrproc_t) xdr_long, &sum, wait);
		if (rv != RPC_SUCCESS) {
			log_fail("clnt_call failed: %s", clnt_sperrno(rv));
			success = 0;
		} else 
		if (sum != expect_sum) {
			log_fail("clnt_call(SUMPROC) returned %ld, expected %ld", sum, expect_sum);
			success = 0;
		}

		clnt_destroy(clnt);
	}

	return success;
}


int
rpctest_verify_getrpcport(rpcprog_t prog, rpcvers_t vers, int ipproto)
{
	const char *netid;
	unsigned long port;

	if (ipproto == IPPROTO_UDP)
		netid = "udp";
	else if (ipproto == IPPROTO_TCP)
		netid = "tcp";
	else
		log_fatal("%s: bad ipproto %u", __FUNCTION__, ipproto);

	/* FIXME: find out whether we registered this program/version and
	   if so get the expected port number
	 */
	port = getrpcport("localhost", prog, vers, ipproto);
	if (port == 0) {
		log_fail("getrpcport(%lu, %lu, %s) failed",
				prog, vers, netid);
		return 0;
	}

	return 1;
}
