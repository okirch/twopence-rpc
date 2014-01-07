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
 * -T <nettype>
 *	Can be one of the common nettypes (like netpath, visible, circuit_n, circuit_v,
 *	datagram_n, datagram_v, tcp, and udp).
 *	Specifying -T with an empty string causes it to call svc_reg with a NULL nettype.
 *
 */
#include "rpctest.h"
#include <getopt.h>

int
main(int argc, char **argv)
{
	const char *opt_hostname = NULL;
	const char *opt_nettype[16];
	unsigned int num_nettypes = 0;
	int c;

	while ((c = getopt(argc, argv, "h:T:")) != EOF) {
		switch (c) {
		case 'h':
			opt_hostname = optarg;
			break;

		case 'T':
			if (optarg == '\0')
				optarg = NULL;
			opt_nettype[num_nettypes++] = optarg;
			break;

		default:
		usage:
			fprintf(stderr,
				"Usage:\n"
				"rpc.squared [-h hostname] [-T nettype]\n");
			return 1;
		}
	}

	if (optind != argc)
		goto usage;

	if (num_nettypes) {
		unsigned int i = 0;

		for (i = 0; i < num_nettypes; ++i) {
			const char *nettype = opt_nettype[i];

			if (!rpctest_register_service_nettype(SQUARE_PROG, SQUARE_VERS, square_prog_1, nettype))
				return 1;
		}
	} else {
		rpctest_run_oldstyle(SQUARE_PROG, SQUARE_VERS, square_prog_1);
	}

	svc_run();
	exit(1);
}
