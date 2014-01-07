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
 * Utility for testing RPCB_GETADDR
 */
#include <getopt.h>
#include <rpc/rpc.h>
#include <netdb.h>
#include "rpctest.h"

enum {
	OPT_HOSTNAME,
	OPT_NETID,
	OPT_OWNER,
	OPT_NOHINT,
	OPT_NONETID,
	OPT_USE_GETVERSADDR,
	OPT_FAIL_UNREG,
};

static struct option	long_options[] = {
	{ "hostname",		required_argument,	NULL,	OPT_HOSTNAME	},
	{ "netid",		required_argument,	NULL,	OPT_NETID	},
	{ "owner",		required_argument,	NULL,	OPT_OWNER	},
	{ "no-hint",		no_argument,		NULL,	OPT_NOHINT	},
	{ "no-netid",		no_argument,		NULL,	OPT_NONETID	},
	{ "use-getversaddr",	no_argument,		NULL,	OPT_USE_GETVERSADDR },
	{ "fail-unknown",	no_argument,		NULL,	OPT_FAIL_UNREG	},

	{ NULL }
};

static void		usage(void);
static unsigned int	get_program(const char *);
static int		uaddr_valid(const char *, const char *);

int
main(int argc, char **argv)
{
	char *opt_hostname = "localhost";
	char *opt_netid = "udp";
	char *opt_owner = "getaddr";
	int opt_nohint = 0;
	int opt_nonetid = 0;
	int opt_use_getversaddr = 0;
	int opt_fail_unreg = 0;
	unsigned int query_program = 0;
	unsigned int query_version = 0;
	int c;

	while ((c = getopt_long(argc, argv, "", long_options, NULL)) != EOF) {
		switch (c) {
		case OPT_HOSTNAME:
			opt_hostname = optarg;
			break;

		case OPT_NETID:
			opt_netid = optarg;
			break;

		case OPT_NOHINT:
			opt_nohint = 1;
			break;

		case OPT_NONETID:
			opt_nonetid = 1;
			break;

		case OPT_OWNER:
			opt_owner = optarg;
			break;

		case OPT_USE_GETVERSADDR:
			opt_use_getversaddr = 1;
			break;

		case OPT_FAIL_UNREG:
			opt_fail_unreg = 1;
			break;

		default:
			log_error("bad argument");
			usage();
		}
	}

	if (optind >= argc)
		usage();
	query_program = get_program(argv[optind++]);

	if (optind < argc)
		query_version = strtoul(argv[optind++], NULL, 0);

	if (optind < argc)
		usage();

	{
		struct timeval timeout = { 10, 0 };
		char *rpcb_uaddr, *server_uaddr = NULL;
		CLIENT *clnt;
		RPCB query;
		int status;

		clnt = rpctest_rpcb_client(opt_netid, opt_hostname, RPCBVERS4, &rpcb_uaddr);
		if (clnt == NULL)
			log_fatal("Unable to create rpcb client handle");

		query.r_prog = query_program;
		query.r_vers = query_version;
		query.r_netid = opt_nonetid? "" : opt_netid;
		query.r_addr = opt_nohint? "" : rpcb_uaddr;
		query.r_owner = opt_owner;

		log_test("Calling getaddr(\"%s\", %u, %u, owner=\"%s\", addr-hint=\"%s\") at host %s via %s",
				query.r_netid, query.r_prog, query.r_vers, query.r_owner, query.r_addr,
				rpcb_uaddr, opt_netid);
		status = clnt_call(clnt,
					opt_use_getversaddr? RPCBPROC_GETVERSADDR : RPCBPROC_GETADDR,
					(xdrproc_t) xdr_rpcb, (char *)(void *)&query,
					(xdrproc_t) xdr_wrapstring, (char *)(void *) &server_uaddr,
					timeout);

		if (status != RPC_SUCCESS) {
			log_fail("getaddr(%s, %u, %u, %s) %s", query.r_netid, query.r_prog, query.r_vers, rpcb_uaddr,
					clnt_sperror(clnt, "failed"));
		} else if (server_uaddr[0] == '\0') {
			if (opt_fail_unreg)
				log_fail("getaddr(%s, %u, %u, %s): not registered",
						query.r_netid, query.r_prog, query.r_vers, rpcb_uaddr);
			else
				printf("NOREG\n");
		} else if (!uaddr_valid(opt_netid, server_uaddr)) {
			log_fail("getaddr(%s, %u, %u, %s): %s - not a valid %s uaddr",
					query.r_netid, query.r_prog, query.r_vers, rpcb_uaddr,
					server_uaddr, opt_netid);
		} else {
			printf("%s\n", server_uaddr);
		}

		free(server_uaddr);
		free(rpcb_uaddr);

		clnt_destroy(clnt);
	}

	return num_fails != 0;
}

static void
usage(void)
{
	fprintf(stderr,
		"Usage: getaddr [options] program [version]\n"
		"Available options:\n"
		"  --hostname <name>\n"
		"         Specify the host name or address to contact (default localhost)\n"
		"  --netid <name>\n"
		"         Specify the netid of the transport to use (default udp)\n"
		"  --owner <name>\n"
		"         Specify the owner to use in the query\n"
		"  --no-hint\n"
		"         Leave out the address in the getaddr query\n"
		"  --no-netid\n"
		"         Leave out the netid in the getaddr query\n"
		"  --use-getversaddr\n"
		"         Use the RPCB_GETVERSADDR instead of RPCB_GETADDR\n"
	       );
	exit(1);
}

static unsigned int
get_program(const char *name)
{
	const char *end;
	unsigned int pnum;

	pnum = strtoul(name, (char **) &end, 0);
	if (*end != '\0') {
		struct rpcent *re;

		re = getrpcbyname(name);
		if (re != 0)
			pnum = re->r_number;
	}

	if (pnum == 0)
		log_fatal("Unable to parse RPC program name/number \"%s\"", name);

	return pnum;
}

static int
uaddr_valid(const char *netid, const char *uaddr)
{
	struct netconfig *nconf = NULL;
	struct netbuf *addr = NULL;

	nconf = getnetconfigent(netid);
	if (!nconf) {
		log_fail("unknown netid %s", netid);
		return 0;
	}

	addr = uaddr2taddr(nconf, uaddr);
	freenetconfigent(nconf);

	if (addr == NULL)
		return 0;

	free(addr->buf);
	free(addr);
	return 1;
}
