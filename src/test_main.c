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
 * Test suite main function
 */
#include <getopt.h>
#include "rpctest.h"

static int	opt_flags;

int
main(int argc, char **argv)
{
	const char *opt_hostname = NULL, *opt_testbus_prefix = NULL;
	int c;

	while ((c = getopt(argc, argv, "Dh:qT:")) != EOF) {
		switch (c) {
		case 'D':
			opt_flags |= RPF_DISPUTED;
			break;

		case 'h':
			opt_hostname = optarg;
			break;

		case 'q':
			opt_flags |= RPF_QUIET;
			break;

		case 'T':
			opt_testbus_prefix = optarg;
			opt_flags |= RPF_TESTBUS;
			break;

		default:
		usage:
			fprintf(stderr,
				"Usage:\n"
				"rpctest [-h hostname]\n");
			return 1;
		}
	}

	if (optind != argc)
		goto usage;

	if (opt_flags & RPF_QUIET)
		log_quiet();
	if (opt_flags & RPF_TESTBUS)
		log_format_testbus(opt_testbus_prefix);

	if (!rpctest_init_nettypes())
		return 1;

	rpctest_verify_netpath_all();
	rpctest_verify_netconfig_all();
	rpctest_verify_sockets_all();
	rpctest_verify_pmap_all(opt_flags);
	rpctest_verify_rpcb_all(opt_flags);
	rpctest_verify_svc_register();
	rpctest_verify_clnt_funcs();

	printf("====\nSummary:\n"
		"%4u tests\n"
		"%4u failures\n"
		"%4u warnings\n",
		num_tests, num_fails, num_warns);

	return num_fails != 0;
}
