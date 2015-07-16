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
 * Main client function
 */
#include <getopt.h>
#include "square.h"

extern int	do_stress(const char *hostname, const char *netid, int argc, char **argv);

int
main(int argc, char **argv)
{
	const char *opt_hostname = "localhost";
	const char *opt_ipproto = NULL;
	int opt_runtime = -1;
	CLIENT *clnt;
	int c;

	while ((c = getopt(argc, argv, "h:TU")) != EOF) {
		switch (c) {
		case 'h':
			opt_hostname = optarg;
			break;

		case 'T':
			opt_ipproto = "tcp";
			break;

		case 'U':
			opt_ipproto = "udp";
			break;

		default:
		usage:
			fprintf(stderr,
				"Usage:\n"
				"square [-h hostname] num ...\n");
			return 1;
		}
	}

	if (optind == argc)
		goto usage;

	if (!strcmp(argv[optind], "stress")) {
		return do_stress(opt_hostname, opt_ipproto, argc - optind, argv + optind);
	}

	clnt = clnt_create(opt_hostname, SQUARE_PROG, SQUARE_VERS, opt_ipproto? : "udp");
	if (clnt == NULL) {
		clnt_pcreateerror("unable to create client");
		return 1;
	}

	while (optind < argc) {
		square_in in;
		square_out *outp;

		in.arg1 = strtol(argv[optind++], NULL, 0);
		outp = squareproc_1(&in, clnt);
		if (outp == NULL) {
			clnt_perror(clnt, "rpc call failed");
			return 1;
		}

		printf("%6ld^2 = %6lu\n",
				in.arg1, outp->res1);
	}

	return 0;
}
