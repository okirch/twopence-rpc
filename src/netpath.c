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
 * Verify NETPATH functions
 */
#include <netconfig.h>
#include "rpctest.h"

static const char *	__netpath_tests[] = {
	"udp",
	"tcp",
	"tcp:udp6",
	"local:tcp:udp",
	"local:tcp::udp",

	NULL
};

static void	rpctest_verify_netpath(const char **, unsigned int);
static int	split_array(const char *, const char **, unsigned int);

void
rpctest_verify_netpath_all(void)
{
	const char *defpath[16];
	int count;

	log_test_group("netpath", "Verify netpath functions");

	log_test("verify default netpath");
	count = rpctest_expand_nettype("netpath", defpath, 16);
	if (!count)
		log_fail("failed");
	else
		rpctest_verify_netpath(defpath, count);

	log_test("verify empty netpath");
	setenv("NETPATH", "", 1);
	rpctest_verify_netpath(NULL, 0);

	{
		unsigned int i = 0;
		const char *path;

		while ((path = __netpath_tests[i++]) != NULL) {
			log_test("verify netpath \"%s\"", path);
			count = split_array(path, defpath, 16);
			if (!count) {
				log_fail("failed");
			} else {
				setenv("NETPATH", path, 1);
				rpctest_verify_netpath(defpath, count);
			}
		}
	}
}

static void
rpctest_verify_netpath(const char **array, unsigned int count)
{
	void *handle;
	struct netconfig *nconf;
	unsigned int i = 0;

	handle = setnetpath();
	if (!handle) {
		log_fail("setnetpath failed");
		return;
	}

	while ((nconf = getnetpath(handle)) != NULL) {
		if (i >= count) {
			log_fail("getnetpath returns more elements than expected");
			return;
		}
		if (strcmp(nconf->nc_netid, array[i])) {
			log_fail("getnetpath returns unexpected netid (got %s, expect %s)",
					nconf->nc_netid, array[i]);
			return;
		}
		/* log_trace("[%u]: %s", i, array[i]); */
		++i;
	}

	if (i < count) {
		log_fail("getnetpath returns fewer elements than expected");
		return;
	}
	endnetpath(handle);
}

/*
 * This leaks memory, so what.
 */
static int
split_array(const char *path, const char **array, unsigned int max)
{
	char *copy = strdup(path), *s;
	unsigned int i = 0;

	for (s = strtok(copy, ":"); s && i < max; s = strtok(NULL, ":")) {
		array[i++] = s;
	}
	return i;
}
