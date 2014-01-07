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
 * Server procedures for square service
 */

#include "square.h"

square_out *
squareproc_1_svc(square_in *inp, struct svc_req *rqstp)
{
	static square_out out;

	out.res1 = inp->arg1 * inp->arg1;
	return (&out);
}

void *
errnoprog_1_svc(void *inp, struct svc_req *rqstp)
{
	svcerr_noprog(rqstp->rq_xprt);
	return NULL;
}

void *
errprogvers_1_svc(void *inp, struct svc_req *rqstp)
{
	svcerr_progvers(rqstp->rq_xprt, 2, 5);
	return NULL;
}

void *
errnoproc_1_svc(void *inp, struct svc_req *rqstp)
{
	svcerr_noproc(rqstp->rq_xprt);
	return NULL;
}

void *
errdecode_1_svc(void *inp, struct svc_req *rqstp)
{
	svcerr_decode(rqstp->rq_xprt);
	return NULL;
}

void *
errsystemerr_1_svc(void *inp, struct svc_req *rqstp)
{
	svcerr_systemerr(rqstp->rq_xprt);
	return NULL;
}

void *
errweakauth_1_svc(void *inp, struct svc_req *rqstp)
{
	svcerr_weakauth(rqstp->rq_xprt);
	return NULL;
}

void *
sinkproc_1_svc(foodata *inp, struct svc_req *rqstp)
{
	static unsigned int x;

	return &x;
}

unsigned int *
sumproc_1_svc(foodata *inp, struct svc_req *rqstp)
{
	static unsigned int sum;
	unsigned int i;

	for (i = 0, sum = 0; i < inp->buffer.buffer_len; ++i) {
		sum += inp->buffer.buffer_val[i];
	}

	return &sum;
}
