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

static const char *	__ipproto_name(int);

extern int	rpctest_pmap_unset_wildcard(struct sockaddr_in *, rpcprog_t);
extern void	rpctest_verify_pmap_set(struct sockaddr_in *,
				const char *progname,
				const struct pmap *, unsigned int, int);
extern void rpctest_verify_pmap_unset(struct sockaddr_in *,
				const char *program_name,
				const struct pmap *, unsigned int, int);
extern int	rpctest_verify_pmap_registration(struct sockaddr_in *,
				const char *progname,
				const struct pmap *, unsigned int);
static int	__rpctest_verify_pmap_registration(struct sockaddr_in *,
				const struct pmap *, unsigned int);
static void	__rpctest_pmap_unset(struct sockaddr_in *,
				const struct pmap *, unsigned int);

#define BI(prog, vers, proto, port) {\
	.pm_prog = prog, \
	.pm_vers = vers, \
	.pm_prot = proto, \
	.pm_port = port, \
}
static struct pmap	__portmap_binding[] = {
	BI(PMAPPROG,	PMAPVERS,	IPPROTO_TCP,	111),
	BI(PMAPPROG,	PMAPVERS,	IPPROTO_UDP,	111),
	BI(RPCBPROG,	RPCBVERS,	IPPROTO_TCP,	111),
	BI(RPCBPROG,	RPCBVERS,	IPPROTO_UDP,	111),
	BI(RPCBPROG,	RPCBVERS4,	IPPROTO_TCP,	111),
	BI(RPCBPROG,	RPCBVERS4,	IPPROTO_UDP,	111),
};

static struct pmap	__square_binding[] = {
	BI(SQUARE_PROG,	2,		IPPROTO_TCP,	5050),
	BI(SQUARE_PROG,	2,		IPPROTO_UDP,	5050),
	BI(SQUARE_PROG,	4,		IPPROTO_UDP,	5051),
	BI(SQUARE_PROG,	4,		IPPROTO_UDP,	5051),
};

static struct pmap	__square_binding_root[] = {
	BI(SQUARE_PROG,	2,		IPPROTO_TCP,	150),
	BI(SQUARE_PROG,	2,		IPPROTO_UDP,	150),
	BI(SQUARE_PROG,	4,		IPPROTO_UDP,	151),
	BI(SQUARE_PROG,	4,		IPPROTO_UDP,	151),
};

void
rpctest_verify_pmap_all(unsigned int flags)
{
	struct sockaddr_in sin;

	log_test_group("portmap", "Verify portmap client functions");

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	rpctest_verify_pmap_registration(&sin, "portmapper", __portmap_binding, 6);

	rpctest_pmap_unset_wildcard(&sin, SQUARE_PROG);

	rpctest_verify_pmap_set(&sin, "square program", __square_binding, 4, 0);
	rpctest_verify_pmap_unset(&sin, "square program", __square_binding, 4, 0);

	rpctest_pmap_unset_wildcard(&sin, SQUARE_PROG);

	if (flags & RPF_DISPUTED) {
		if (geteuid() == 0) {
			rpctest_verify_pmap_set(&sin, "square program (with privports)", __square_binding_root, 4, 0);
			rpctest_verify_pmap_unset(&sin, "square program (with privports)", __square_binding_root, 4, 0);

			rpctest_pmap_unset_wildcard(&sin, SQUARE_PROG);

			rpctest_drop_privileges();
			rpctest_verify_pmap_set(&sin, "square program (with privports, unpriv user)", __square_binding_root, 4, 1);
			rpctest_resume_privileges();
		} else {
			rpctest_verify_pmap_set(&sin, "square program (with privports, unpriv user)", __square_binding_root, 4, 1);
		}
	}

	rpctest_pmap_unset_wildcard(&sin, SQUARE_PROG);
}

void
rpctest_verify_pmap_set(struct sockaddr_in *srv_addr, const char *program_name,
			const struct pmap *pmaps, unsigned int count,
			int should_fail)
{
	const struct pmap *pm;
	unsigned int i;

	log_test("Verifying pmap_set operation for %s", program_name);
	for (i = 0, pm = pmaps; i < count; ++i, ++pm) {
		if (!pmap_set(pm->pm_prog, pm->pm_vers, pm->pm_prot, pm->pm_port)) {
			if (should_fail)
				continue;
			log_fail("unable to register %u/%u/%s, port %lu",
					pm->pm_prog, pm->pm_vers,
					__ipproto_name(pm->pm_prot),
					pm->pm_port);
			return;
		} else if (should_fail) {
			log_fail("succeeded to register %u/%u/%s, port %lu (should have failed)",
					pm->pm_prog, pm->pm_vers,
					__ipproto_name(pm->pm_prot),
					pm->pm_port);
		}
	}

	if (should_fail)
		__rpctest_verify_pmap_registration(srv_addr, NULL, 0);
	else
		__rpctest_verify_pmap_registration(srv_addr, pmaps, count);
}

void
rpctest_verify_pmap_unset(struct sockaddr_in *srv_addr, const char *program_name,
			const struct pmap *pmaps, unsigned int count,
			int should_fail)
{
	log_test("Verifying pmap_unset operation for %s", program_name);
	__rpctest_pmap_unset(srv_addr, pmaps, count);

	if (!should_fail)
		__rpctest_verify_pmap_registration(srv_addr, NULL, 0);
}

int
rpctest_verify_pmap_registration(struct sockaddr_in *srv_addr, const char *program_name,
			const struct pmap *pmaps, unsigned int count)
{
	log_test("Verifying pmap registration for %s (expect %u regs)", program_name, count);
	return __rpctest_verify_pmap_registration(srv_addr, pmaps, count);
}

int
__rpctest_verify_pmap_registration(struct sockaddr_in *srv_addr,
			const struct pmap *pmaps, unsigned int count)
{
	const struct pmap *pm;
	struct pmaplist *pml;
	unsigned int i;

	for (i = 0, pm = pmaps; i < count; ++i, ++pm) {
		u_long port;

		port = pmap_getport(srv_addr, pm->pm_prog, pm->pm_vers, pm->pm_prot);
		if (port == 0) {
			log_fail("No registration for %u/%u/%s",
					pm->pm_prog, pm->pm_vers,
					__ipproto_name(pm->pm_prot));
			return 0;
		}

		if (pm->pm_port && port != pm->pm_port) {
			log_fail("Incorrect registration for %u/%u/%s - expected %lu, found %lu",
					pm->pm_prog, pm->pm_vers,
					__ipproto_name(pm->pm_prot),
					pm->pm_port, port);
			return 0;
		}
	}

	pml = pmap_getmaps(srv_addr);
	if (pml == NULL) {
		log_fail("pmap_getmaps failed");
		return 0;
	}

	while (pml != NULL) {
		struct pmaplist *next = pml->pml_next;
		int program_match = 0, exact_match = 0;
		struct pmap *reg = &pml->pml_map;

		for (i = 0, pm = pmaps; i < count; ++i, ++pm) {
			if (pm->pm_prog != reg->pm_prog)
				continue;

			program_match++;
			if (pm->pm_vers != reg->pm_vers
			 || pm->pm_prot != reg->pm_prot)
				continue;

			exact_match = 1;
			break;
		}

		if (program_match && !exact_match) {
			log_fail("Found unexpected registration %u/%u/%s",
					reg->pm_prog, reg->pm_vers,
					__ipproto_name(reg->pm_prot));
		}

		free(pml);
		pml = next;
	}

	return 1;
}

/*
 * Unset all versions of a given RPC program
 */
int
rpctest_pmap_unset_wildcard(struct sockaddr_in *srv_addr, rpcprog_t prog)
{
	struct pmap pmaps[16];
	struct pmaplist *pml, *next;
	unsigned int i = 0;

	pml = pmap_getmaps(srv_addr);
	if (pml == NULL) {
		log_error("pmap_getmaps failed");
		return 0;
	}

	for (; pml && ((next = pml->pml_next) || 1); pml = next) {
		struct pmap *reg = &pml->pml_map;

		if (reg->pm_prog == prog) {
			if (i > 16) {
				log_fail("%s: too many registrations", __FUNCTION__);
				return 0;
			}
			pmaps[i++] = pml->pml_map;
		}

		free(pml);
	}

	__rpctest_pmap_unset(srv_addr, pmaps, i);
	return 1;
}

void
__rpctest_pmap_unset(struct sockaddr_in *srv_addr,
			const struct pmap *pmaps, unsigned int count)
{
	const struct pmap *pm;
	unsigned int i, j;

	for (i = 0, pm = pmaps; i < count; ++i, ++pm) {
		for (j = 0; j < i; ++j) {
			if (pm->pm_prog == pmaps[j].pm_prog
			 && pm->pm_vers == pmaps[j].pm_vers)
				goto skip_dupe;
		}

		if (!pmap_unset(pm->pm_prog, pm->pm_vers)) {
			log_fail("pmap_unset(%lu, %lu) failed", pm->pm_prog, pm->pm_vers);
			return;
		}

skip_dupe: ;
	}
}

static const char *
__ipproto_name(int proto)
{
	static char buffer[64];

	switch (proto) {
	case IPPROTO_UDP:
		return "udp";
	case IPPROTO_TCP:
		return "tcp";
	default:
		snprintf(buffer, sizeof(buffer), "ipproto%u", proto);
		return buffer;
		return "unknown";
	}
}
