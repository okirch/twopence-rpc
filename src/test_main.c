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
	static struct option options[] = {
		{ "with-disputed",	no_argument,		NULL,	'D' },
		{ "hostname",		required_argument,	NULL,	'h' },
		{ "quiet",		no_argument,		NULL,	'q' },
		{ "log-format",		required_argument,	NULL,	'l' },
		{ "log-file",		required_argument,	NULL,	'f' },
		{ NULL }
	};
	const char *opt_hostname = NULL;
	const char *opt_log_format = NULL;
	const char *opt_log_file = NULL;
	int c;

	while ((c = getopt_long(argc, argv, "Dh:ql:f:", options, NULL)) != EOF) {
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

		case 'l':
			opt_log_format = optarg;
			break;

		case 'f':
			opt_log_file = optarg;
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

	log_init(opt_log_format, "rpcunit", opt_log_file);
	if (!rpctest_init_nettypes())
		return 1;

	rpctest_verify_netpath_all();
	rpctest_verify_netconfig_all();
	rpctest_verify_sockets_all();
	rpctest_verify_pmap_all(opt_flags);
	rpctest_verify_rpcb_all(opt_flags);
	rpctest_verify_svc_register();
	rpctest_verify_clnt_funcs();

	log_finish();

	return num_fails != 0;
}
