/*
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
 */
#ifndef RPCTEST_H
#define RPCTEST_H

#include "square.h"

#define __TEST_RPC_SOCKINFO

#define SQUARE_LOCAL_ADDR	"/var/tmp/square.sock"

#define ARRAY_COUNT(__array)	(sizeof(__array) / sizeof((__array)[0]))

#define RPF_DISPUTED	0x0001
#define RPF_QUIET	0x0002
#define RPF_TESTBUS	0x0004

typedef struct rpctest_process rpctest_process_t;

extern unsigned int	num_tests;
extern unsigned int	num_fails;
extern unsigned int	num_warns;

typedef void	rpc_program_fn_t(struct svc_req *, register SVCXPRT *);

extern void	log_quiet(void);
extern void	log_format_testbus(void);
extern void	log_test_group(const char *, const char *, ...);
extern void	log_test(const char *, ...);
extern void	log_fail(const char *, ...);
extern void	log_warn(const char *, ...);
extern void	log_trace(const char *, ...);
extern void	log_error(const char *, ...);
extern void	log_fatal(const char *, ...) __attribute((noreturn));

extern void	square_prog_1(struct svc_req *, register SVCXPRT *);

extern int	rpctest_init_nettypes(void);
extern int	rpctest_expand_nettype(const char *, const char **, unsigned int);
extern const char **rpctest_get_nettypes(void);
extern int	rpctest_make_socket(const char *);

extern void	rpctest_verify_netpath_all(void);
extern void	rpctest_verify_netconfig_all(void);
extern void	rpctest_verify_sockets_all(void);
extern void	rpctest_verify_pmap_all(unsigned int);
extern void	rpctest_verify_rpcb_all(unsigned int);
extern void	rpctest_verify_svc_register(void);
extern void	rpctest_verify_clnt_funcs(void);

extern int	rpctest_run_oldstyle(unsigned long, unsigned long, rpc_program_fn_t *);
extern int	rpctest_register_service_nettype(unsigned long, unsigned long, rpc_program_fn_t *, const char *);
extern CLIENT *	rpctest_rpcb_client(const char *, const char *, unsigned int, char **);
extern RPCB *	rpctest_rpcb_get_registrations(rpcprog_t);
extern int	rpctest_verify_rpcb_registration(const char *, const struct rpcb *);
extern int	rpctest_rpcb_unset_wildcard(rpcprog_t);
extern int	rpctest_svc_register_rpcb(const RPCB *, rpc_program_fn_t *);
extern void	rpctest_svc_cleanup(RPCB *);

extern void	rpctest_drop_privileges(void);
extern void	rpctest_resume_privileges(void);
extern rpctest_process_t *rpctest_fork_server(void);
extern int	rpctest_kill_process(rpctest_process_t *);
extern int	rpctest_try_catch_crash(int *termsig, int *exit_code);
extern const struct sockaddr *loopback_address(int af);
extern const struct sockaddr_un *build_local_address(const char *);
extern struct netbuf *rpctest_get_static_netbuf(size_t size);
extern struct netbuf *rpctest_get_static_netbuf_canary(size_t size, unsigned int ncanaries);
extern int	rpctest_verify_netbuf_canary(struct netbuf *nb, unsigned int ncanaries);
extern int	rpctest_verify_status(int, CLIENT *, enum clnt_stat);

struct ifaddrs;
extern const char *sockaddr_ntoa(const struct sockaddr *, const struct ifaddrs *);

extern const char *printable(const char *);

#endif /* RPCTEST_H */
