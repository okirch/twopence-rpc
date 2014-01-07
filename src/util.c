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
 * Utility functions
 */
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <ctype.h>
#include "rpctest.h"

struct rpctest_process {
	pid_t	pid;
};

void
rpctest_drop_privileges(void)
{
	if (getuid() != 0 || geteuid() != 0)
		log_fatal("%s: I don't seem to be root", __FUNCTION__);

	setreuid(0, 65534);
	if (getuid() != 0)
		log_fatal("lost real uid 0");
	if (geteuid() != 65534)
		log_fatal("unable to set euid to -2");
}

void
rpctest_resume_privileges(void)
{
	setreuid(0, 0);
	if (getuid() != 0)
		log_fatal("lost real uid 0");
	if (geteuid() != 0)
		log_fatal("lost effective uid 0");
}

rpctest_process_t *
rpctest_fork_server(void)
{
	rpctest_process_t *proc;
	pid_t pid;

	proc = calloc(1, sizeof(*proc));

	pid = fork();
	if (pid < 0)
		log_fatal("%s: unable to fork: %m", __FUNCTION__);

	if (pid == 0) {
		svc_run();
		exit(1);
	}

	proc->pid = pid;
	return proc;
}

int
rpctest_kill_process(rpctest_process_t *proc)
{
	int ret = 1;

	signal(SIGCHLD, SIG_IGN);
	if (kill(proc->pid, SIGTERM) < 0) {
		log_error("unable to kill child process");
		ret = 0;
	}
	free(proc);

	return ret;
}

/*
 * Use this to run something and catch a crash
 * Returns 0 for the child process, negative on error
 * and positive for success.
 */
int
rpctest_try_catch_crash(int *termsig)
{
	pid_t pid;
	int status;

	signal(SIGCHLD, SIG_DFL);

	pid = fork();
	if (pid < 0) {
		log_fail("unable to fork: %m");
		return -1;
	}

	if (pid == 0) {
		int fd = open("/dev/null", O_RDWR);

		if (fd >= 0) {
			dup2(fd, 1);
			dup2(fd, 2);
		} else {
			log_error("/dev/null: %m");
		}

		return 0;
	}

	if (waitpid(pid, &status, 0) < 0) {
		log_fail("waitpid failed: %m");
		return -1;
	}

	*termsig = WIFSIGNALED(status)? WTERMSIG(status) : 0;
	return 1;
}

struct netbuf *
rpctest_get_static_netbuf_canary(size_t size, unsigned int ncanaries)
{
	static uint32_t addrbuf[256];
	static struct netbuf nb;
	unsigned int i, nwords;

	nwords = 2 * ncanaries + (size + 3) / 4;
	if (4 * nwords > sizeof(addrbuf))
		log_fatal("%s: requested buffer size too big (size=%u, ncanaries=%u)",
				__FUNCTION__, size, ncanaries);

	memset(addrbuf, 0xA5, sizeof(addrbuf));
	for (i = 0; i < ncanaries; ++i)
		addrbuf[i] = addrbuf[nwords - 1 - i] = 0xdeadbeef;

	memset(&nb, 0, sizeof(nb));
	nb.len = nb.maxlen = size;
	nb.buf = addrbuf + ncanaries;
	return &nb;
}

int
rpctest_verify_netbuf_canary(struct netbuf *nb, unsigned int ncanaries)
{
	const uint32_t *addrbuf;
	unsigned int i, nwords;

	addrbuf = ((const uint32_t *) nb->buf) - ncanaries;
	nwords = ((nb->maxlen + 3) / 4) + 2 * ncanaries;

	for (i = 0; i < ncanaries; ++i) {
		if (addrbuf[i] != 0xdeadbeef) {
			log_error("%s: dead canary at buf[%u] = %x", __FUNCTION__,
					i, addrbuf[i]);
			return 0;
		}
		if (addrbuf[nwords - 1 - i] != 0xdeadbeef) {
			log_error("%s: dead canary at buf[%u] = %x", __FUNCTION__,
					nwords - 1 - i, addrbuf[nwords - 1 - i]);
			return 0;
		}
	}

	return 1;
}

struct netbuf *
rpctest_get_static_netbuf(size_t size)
{
	return rpctest_get_static_netbuf_canary(size, 0);
}

/*
 * Variant of clnt_sperrno which strips off the "RPC: " tag
 */
const char *
rpctest_sperrno(enum clnt_stat stat)
{
	const char *errstr;

	errstr = clnt_sperrno(stat);
	if (errstr && !strncmp(errstr, "RPC: ", 5))
		errstr += 5;
	return errstr;
}

/*
 * Check whether an error returned by the rpc library matches our
 * expectations
 */
int
rpctest_verify_status(int is_error, CLIENT *clnt, enum clnt_stat expected)
{
	enum clnt_stat status;

	if (is_error == 0) {
		status = RPC_SUCCESS;
	} else if (clnt == NULL) {
		status = rpc_createerr.cf_stat;
	} else {
		struct rpc_err e;

		clnt_geterr(clnt, &e);
		status = e.re_status;
	}

	if (is_error && status == RPC_SUCCESS) {
		log_fail("call failed, but clnt_stat is RPC_SUCCESS");
		return 0;
	}

	if (status == expected)
		return 1;

	if (is_error == 0) {
		log_fail("call succeeded - expected error %u (%s)",
				expected, rpctest_sperrno(expected));
	} else if (expected == RPC_SUCCESS) {
		log_fail("call failed with error %u (%s) - expected success",
				status, rpctest_sperrno(status));
	} else {
		log_fail("expected error %u (%s), got error %u (%s)",
				expected, rpctest_sperrno(expected),
				status, rpctest_sperrno(status));
	}
	return 0;
}

const struct sockaddr *
loopback_address(int af)
{
	static struct sockaddr_in sin;
	static struct sockaddr_in6 six;

	if (sin.sin_family == AF_UNSPEC) {
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		six.sin6_family = AF_INET6;
		six.sin6_addr = in6addr_loopback;
	}

	switch (af) {
	case AF_INET:
		return (struct sockaddr *) &sin;
	case AF_INET6:
		return (struct sockaddr *) &six;
	}

	return NULL;
}


extern const struct sockaddr_un *
build_local_address(const char *path)
{
	static struct sockaddr_un sun;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strncpy(sun.sun_path, path, sizeof(sun.sun_path));
	return &sun;
}

const char *
sockaddr_ntoa(const struct sockaddr *sa, const struct ifaddrs *ifa)
{
	static char addrbuf[INET6_ADDRSTRLEN + 20];

	switch (sa->sa_family) {
	case AF_INET:
		if (!inet_ntop(AF_INET, &((struct sockaddr_in *) sa)->sin_addr, addrbuf, sizeof(addrbuf)))
			log_fatal("Cannot represent IPv4 addr");
		break;

	case AF_INET6:
		if (!inet_ntop(AF_INET6, &((struct sockaddr_in6 *) sa)->sin6_addr, addrbuf, sizeof(addrbuf)))
			log_fatal("Cannot represent IPv6 addr");

		/* For link-local addrs, make sure we giv'em the
		 * interface index, as in
		 * fe80::21c:c4ff:fe83:3fce%eth0
		 */
		if (!strncmp(addrbuf, "fe80:", 5) && ifa && ifa->ifa_name) {
			unsigned int len = strlen(addrbuf);

			snprintf(addrbuf + len, sizeof(addrbuf) - len, "%%%s", ifa->ifa_name);
		}
		break;

	case AF_LOCAL:
		return ((const struct sockaddr_un *) sa)->sun_path;

	default:
		log_fatal("sockaddr_ntoa: unsupported address family %u", sa->sa_family);
	}

	return addrbuf;
}

const char *
printable(const char *string)
{
	static char *buffer = NULL;
	const char *s;
	char *p;

	for (s = string; *s; ++s) {
		if (!isprint(*s))
			goto need_to_escape;
	}
	return string;

need_to_escape:
	if (buffer)
		free(buffer);
	p = buffer = malloc(3 * strlen(string) + 1);
	for (s = string; *s; ++s) {
		char cc = *s;

		if (isprint(cc)) {
			*p++ = cc;
		} else {
			switch (cc) {
			case '\n': strcpy(p, "\\n"); break;
			case '\r': strcpy(p, "\\r"); break;
			case '\t': strcpy(p, "\\t"); break;
			default: sprintf(p, "\\%03o", cc);
			}
		}
	}
	*p = '\0';
	return buffer;
}
