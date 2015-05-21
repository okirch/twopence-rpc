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
 * Verify nettype functions
 */
#include "rpctest.h"
#include "square.h"
#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* libtirpc internals */
extern void *	__rpc_setconf(const char *nettype);
extern struct netconfig *__rpc_getconf(void *vhandle);
extern void	__rpc_endconf(void *vhandle);

#define RPCTEST_NETID_MAX	16

static struct nettype {
	const char *	name;
	unsigned int	numids;
	char *		netid[RPCTEST_NETID_MAX];
} __all_nettypes[] = {
	{ "netpath" },
	{ "visible" },
	{ "circuit_n" },
	{ "datagram_n" },
	{ "circuit_v" },
	{ "datagram_v" },
	{ "udp" },
	{ "tcp" },

	{ NULL}
};

/*
 * Handle nettypes. We build a static map of nettypes -> netids at
 * startup.
 */
static int
rpctest_build_nettype(struct nettype *nt)
{
	void *handle;
	int rv = 0;

	handle = __rpc_setconf(nt->name);
	if (handle == NULL) {
		fprintf(stderr,
			"Cannot look up nettype <%s>\n", nt->name);
		return 0;
	}

	nt->numids = 0;
	while (1) {
		struct netconfig *nconf;

		if ((nconf = __rpc_getconf(handle)) == NULL) {
			if (rpc_createerr.cf_stat != RPC_SUCCESS) {
				fprintf(stderr,
					"Failed to enumerate nettype <%s>: %s\n",
					nt->name, clnt_spcreateerror(""));
				goto out;
			}
			break;
		}

		if (nt->numids >= RPCTEST_NETID_MAX) {
			fprintf(stderr,
				"Too many netids for nettype <%s>\n", nt->name);
			goto out;
		}
		nt->netid[nt->numids++] = strdup(nconf->nc_netid);
	}

	rv = 1;

out:
	__rpc_endconf(handle);
	return rv;
}

int
rpctest_init_nettypes(void)
{
	struct nettype *nt;

	unsetenv("NETPATH");

	for (nt = __all_nettypes; nt->name; ++nt) {
		if (!rpctest_build_nettype(nt))
			return 0;
	}

	return 1;
}

const char **
rpctest_get_nettypes(void)
{
	static const char *nettypes[16];

	if (nettypes[0] == NULL) {
		struct nettype *nt;
		unsigned int i = 0;

		for (nt = __all_nettypes; nt->name; ++nt)
			nettypes[i++] = nt->name;
		nettypes[i++] = NULL;
	}

	return nettypes;
}

int
rpctest_expand_nettype(const char *nettype, const char **array, unsigned int max)
{
	struct nettype *nt;

	for (nt = __all_nettypes; nt->name; ++nt) {
		unsigned int i;

		if (strcasecmp(nt->name, nettype))
			continue;

		for (i = 0; i < nt->numids && i < max; ++i)
			array[i] = nt->netid[i];
		return i;
	}

	return 0;
}

int
rpctest_run_oldstyle(unsigned long prog, unsigned long vers, rpc_program_fn_t *progfn)
{
	SVCXPRT *transp;

	pmap_unset(prog, vers);

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL) {
		fprintf (stderr, "cannot create udp service.\n");
		return 0;
	}
	if (!svc_register(transp, prog, vers, progfn, IPPROTO_UDP)) {
		fprintf (stderr, "unable to register (%lu, %lu, udp).\n", prog, vers);
		return 0;
	}

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		fprintf (stderr, "cannot create tcp service.\n");
		return 0;
	}
	if (!svc_register(transp, prog, vers, progfn, IPPROTO_TCP)) {
		fprintf (stderr, "unable to register (%lu, %lu, tcp).\n", prog, vers);
		return 0;
	}

	return 1;
}

int
rpctest_run_newstyle(unsigned long prog, unsigned long vers, rpc_program_fn_t *progfn)
{
	svc_unreg(prog, vers);

	if (svc_create(progfn, prog, vers, NULL) == 0) {
		fprintf (stderr, "cannot create services.\n");
		return 0;
	}

	return 1;
}
