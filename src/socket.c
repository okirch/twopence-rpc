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
 * Verify socket binding functions
 */
#include <netconfig.h>
#include <unistd.h>
#include "rpctest.h"

static struct bind_test {
	const char *	netid;
	int		try_privport;
} __bind_tests[] = {
	{ "udp", 1, },
	{ "tcp", 1, },
	{ "udp6", 1, },
	{ "tcp6", 1, },
	{ "local", 0, },
};

static int	rpctest_verify_bindresvport(int, int);

void
rpctest_verify_sockets_all(void)
{
	struct bind_test *bt;
	int am_root;

	log_test_group("socket", "Verify socket functions");

	am_root = geteuid() == 0;

	for (bt = __bind_tests; bt->netid; ++bt) {
		int fd;

		log_test_tagged(bt->netid, "Trying to create socket for <%s>", bt->netid);
		fd = rpctest_make_socket(bt->netid);
		if (fd < 0) {
			log_fail("failed to create socket: %m");
			continue;
		}

		if (bt->try_privport) {
			int bound = 0;

			/* First, try to bind to a resv port without root privileges.
			 * This should always fail.
			 */
			if (am_root) {
				rpctest_drop_privileges();
				bound = rpctest_verify_bindresvport(fd, 0);
				rpctest_resume_privileges();
			}

			/* Unless the above succeeded unexpectedly, we
			 * try once more. In the case where we're root,
			 * this should succeed - otherwise, this should fail.
			 */
			if (!bound)
				rpctest_verify_bindresvport(fd, am_root);
		}
		close(fd);
	}
}

static int
rpctest_verify_bindresvport(int fd, int should_succeed)
{
	if (bindresvport_sa(fd, NULL) < 0) {
		if (should_succeed) {
			log_fail("bindresvport failed: %m");
			return 0;
		}
	} else {
		struct sockaddr_storage ss;
		socklen_t alen = sizeof(ss);
		uint16_t port;

		if (!should_succeed) {
			log_fail("bindresvport succeeded unexpectedly");
			return 0;
		}

		if (getsockname(fd, (struct sockaddr *) &ss, &alen) < 0) {
			log_fail("getsockname failed: %m");
			return 0;
		}

		switch (ss.ss_family) {
		case AF_INET:
			port = ntohs(((struct sockaddr_in *) &ss)->sin_port);
			break;
		case AF_INET6:
			port = ntohs(((struct sockaddr_in6 *) &ss)->sin6_port);
			break;
		default:
			log_fail("bindresvport bound to unknown address family");
			return 0;
		}

		if (port == 0 || port >= 1024) {
			log_fail("bindresvport bound to port %u", port);
			return 0;
		}
	}
	return 1;
}
