/*
 * RPC Test suite
 *
 * Copyright (C) 2011-2015, Olaf Kirch <okir@suse.de>
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
 * Stress test client.
 *
 * Opens a number of connections, and sends data across.
 *
 * Try this:
 *  ./rpc.sqaured
 *  ./square stress runtime=60 jobs=120 trace=1
 *
 * FIXME:
 *  Introduce UDP jobs
 */

#include <sys/poll.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "rpctest.h"
#include "src/square.h"

#define BASE_PORT		0
#define HIST_MAX		100

struct sumclnt {
	struct sockaddr_storage	svc_addr;
	socklen_t		svc_addrlen;

	/* Max number of calls per TCP connection before we recycle
	 * the connection. */
	unsigned int		max_calls;

	/* Number of calls made */
	unsigned long		ncalls;

	double			job_timeout;

	struct sumjob **	jobs;
	unsigned int		njobs;

	unsigned int		errors;

	struct histogram {
		double		xrange;
		double		xscale;

		unsigned int	values[HIST_MAX];
	} send_histogram, recv_histogram;
};

struct sumjob {
	unsigned int		id;
	char *			name;

	int			fd;
	int			proto;
	struct pollfd *		pollfd;

	struct timeval		ctime;
	struct timeval		timeout;
	uint32_t		xid;

	unsigned int		ncalls;
	unsigned int		max_calls;

	unsigned int		num_ints;

	struct {
		struct timeval	begin;

		unsigned char *	buf;
		unsigned int	size;
		unsigned int	len;
		unsigned int	pos;
	} send;
	struct {
		struct timeval	begin;

		unsigned char *	buf;
		unsigned int	size;
		unsigned int	len;
		unsigned int	pos;
	} recv;

	unsigned int		sum;

	char			last_activity;
};

struct timeout {
	struct timeval		now;
	long			current;
};

static unsigned int	xid = 0x1234abcd;

static int		opt_trace;
static unsigned int	opt_job_timeout = 0;
static unsigned int	opt_max_calls = 32;


static struct sumclnt *	sumclnt_new(const char *hostname, unsigned int njobs);
static void		sumclnt_free(struct sumclnt *clnt);
static int		sumclnt_poll(struct sumclnt *clnt);

static struct sumjob *	sumjob_new(struct sumclnt *clnt, unsigned int jobid, unsigned int num_ints);
static int		sumjob_connect(struct sumclnt *clnt, struct sumjob *job);
static int		sumjob_build_packet(struct sumjob *job);
static void		sumjob_drop_buffers(struct sumjob *job);
static void		sumjob_set_timeout(struct sumclnt *clnt, struct sumjob *job);
static void		sumjob_close(struct sumjob *job);
static int		sumjob_send(struct sumclnt *clnt, struct sumjob *job);
static int		sumjob_recv(struct sumclnt *clnt, struct sumjob *job);
static int		sumjob_check_reply(struct sumjob *job);
static void		sumjob_timeout(struct sumjob *);
static void		sumjob_print(const struct sumjob *);
static void		sumjob_free(struct sumjob *);

static void		timeout_init(struct timeout *, long initial_timeout);
static int		timeout_update(struct timeout *tmo, const struct timeval *expire);
static long		timeout_value(const struct timeout *tmo);

static void		hist_init(struct histogram *h, double xrange);
static void		hist_record(struct histogram *h, const struct timeval *t0);
static void		hist_print(struct histogram *h, unsigned int nlines);

int
do_stress(const char *hostname, const char *netid, int argc, char **argv)
{
	struct sumclnt *clnt;
	time_t end_time = 0;
	unsigned int opt_njobs = 128;
	unsigned int opt_max_errors = 256;
	int exitval = 0;
	int i;

	srandom(getpid());

	for (i = 1; i < argc; ++i) {
		char *name = argv[i];
		char *value;
		long number = -1;

		if ((value = strchr(name, '=')) != NULL)
			*value++ = '\0';

		if (!strcmp(name, "trace")) {
			opt_trace = 1;
			continue;
		}

		if (!strcmp(name, "runtime")
		 || !strcmp(name, "jobs")
		 || !strcmp(name, "job-timeout")
		 || !strcmp(name, "max-calls")
		 || !strcmp(name, "max-errors")) {
			char *s;

			if (!value) {
				log_error("missing value to %s argument", name);
				goto ignore_arg;
			}
			number = strtoul(value, &s, 0);
			if (s && *s) {
				log_error("cannot parse numeric value to %s=%s", name, value);
				goto ignore_arg;
			}

			if (number <= 0) {
				log_error("%s value must not be negative", name);
				goto ignore_arg;
			}
		}

		if (!strcmp(name, "runtime")) {
			end_time = time(NULL) + number;
			continue;
		}
		if (!strcmp(name, "jobs")) {
			opt_njobs = number;
			continue;
		}
		if (!strcmp(name, "job-timeout")) {
			opt_job_timeout = number;
			continue;
		}
		if (!strcmp(name, "max-calls")) {
			opt_max_calls = number;
			continue;
		}
		if (!strcmp(name, "max-errors")) {
			opt_max_errors = number;
			continue;
		}

		log_error("unknown argument \"%s\"", name);
ignore_arg:
		if (value)
			value[-1] = '=';
		log_error("ignoring argument %s", name);
	}

	clnt = sumclnt_new(hostname, opt_njobs);

	/* FIXME: warn if the runtime is smaller than the default job timeout */

	while (1) {
		if (sumclnt_poll(clnt) < 0)
			return 1;

		if (end_time && end_time <= time(NULL))
			break;
		if (clnt->errors >= opt_max_errors) {
			log_error("Too many errors, aborting this run");
			break;
		}
	}

	if (!opt_trace)
		printf("\n");

	if (clnt->errors) {
		printf("Encountered %u errors\n", clnt->errors);
		exitval = 1;
	}

	printf("\n\nSend histogram (time needed to send a full packet)\n");
	hist_print(&clnt->send_histogram, 16);

	printf("\n\nReceive histogram (time taken to receive a full reply)\n");
	hist_print(&clnt->recv_histogram, 16);
 
	sumclnt_free(clnt);
	return exitval;
}

struct sumclnt *
sumclnt_new(const char *hostname, unsigned int njobs)
{
	struct netconfig *nconf;
	struct sumclnt *clnt;
	struct netbuf abuf;

	clnt = calloc(1, sizeof(*clnt));
	clnt->max_calls = opt_max_calls;
	clnt->job_timeout = opt_job_timeout;
	if (clnt->job_timeout == 0)
		clnt->job_timeout = 0.1 * njobs * 2;
	if (clnt->job_timeout < 10)
		clnt->job_timeout = 10;

	abuf.buf = &clnt->svc_addr;
	abuf.len = abuf.maxlen = sizeof(clnt->svc_addr);

	nconf = getnetconfigent("tcp");

	if (!rpcb_getaddr(SQUARE_PROG, SQUARE_VERS, nconf, &abuf, hostname))
		log_fatal("Cannot find square service on host %s", hostname);
	freenetconfigent(nconf);
	clnt->svc_addrlen = abuf.len;

	clnt->jobs = calloc(njobs, sizeof(clnt->jobs[0]));
	clnt->njobs = njobs;

	/* Send histogram is 0..500 msec */
	hist_init(&clnt->send_histogram, 500 * 1e-3);

	/* Recv histogram is 0..500 msec */
	hist_init(&clnt->recv_histogram, 10000 * 1e-3);

	return clnt;
}

void
sumclnt_free(struct sumclnt *clnt)
{
	unsigned int i;

	for (i = 0; i < clnt->njobs; ++i) {
		struct sumjob *job = clnt->jobs[i];

		if (job)
			sumjob_free(job);
		clnt->jobs[i] = NULL;
	}

	free(clnt->jobs);
	free(clnt);
}

int
sumclnt_poll(struct sumclnt *clnt)
{
	struct pollfd *pfd;
	struct timeout timeout;
	unsigned int i;

	timeout_init(&timeout, 10000);

	pfd = alloca(clnt->njobs * sizeof(pfd[0]));
	for (i = 0; i < clnt->njobs; ++i) {
		struct sumjob *job = clnt->jobs[i];

		if (job == NULL) {
			job = sumjob_new(clnt, i, random() % 65536);
			if (job == NULL)
				log_fatal("Unable to create new sum job");
			if (sumjob_connect(clnt, job) < 0)
				log_fatal("Unable to connect to server");
			sumjob_set_timeout(clnt, job);
			clnt->jobs[i] = job;
		}

		pfd[i].fd = job->fd;

		pfd[i].events = 0;
		if (job->send.pos >= job->send.len) {
			/* We already sent everything. */
			pfd[i].events = POLLIN;
		} else {
			pfd[i].events = POLLOUT | POLLHUP;
			job->last_activity = '.';
		}

		pfd[i].events |= POLLERR;
		pfd[i].revents = 0;
		job->pollfd = pfd + i;
	}

	if (poll(pfd, clnt->njobs, timeout_value(&timeout)) < 0)
		log_fatal("poll: %m");

	timeout_init(&timeout, -1);
	for (i = 0; i < clnt->njobs; ++i) {
		struct sumjob *job = clnt->jobs[i];

		if (pfd[i].revents & POLLERR) {
			log_error("%s: detected POLLERR - remote closed connection?", job->name);
			job->last_activity = '*';
			sumjob_close(job);
			clnt->errors++;
			continue;
		}

		if (pfd[i].revents & POLLOUT) {
			if (sumjob_send(clnt, job) < 0)
				log_fatal("Unable to send data");
		} else
		if (pfd[i].revents & POLLIN) {
			if (sumjob_recv(clnt, job) < 0)
				log_fatal("Unable to recv data");
		} else
		if (pfd[i].revents & POLLHUP) {
			log_error("%s: remote closed connection", job->name);
			job->last_activity = '*';
			sumjob_close(job);
			clnt->errors++;
			continue;
		}

		/* Check for job timeout */
		if (timeout_update(&timeout, &job->timeout) < 0) {
			sumjob_timeout(job);
			job->last_activity = 't';
			clnt->errors++;
			continue;
		}
	}

	if (opt_trace) {
		for (i = 0; i < clnt->njobs; ++i) {
			struct sumjob *job = clnt->jobs[i];

			if (!job) {
				printf(".");
			} else {
				printf("%c", job->last_activity);
				job->last_activity = ' ';
			}
		}
		printf(":\n");
		fflush(stdout);
	} else {
		static time_t next_report;
		time_t now = time(NULL);

		if (now >= next_report) {
			printf("%lu... ", clnt->ncalls);
			fflush(stdout);
			next_report = now + 1;
		}
	}

	for (i = 0; i < clnt->njobs; ++i) {
		struct sumjob *job = clnt->jobs[i];

		if (!job)
			continue;

		job->pollfd = NULL;
		if (job->fd < 0) {
			sumjob_free(job);
			clnt->jobs[i] = NULL;
		}
	}

	return 0;
}

static void
sumclnt_record_send_delay(struct sumclnt *clnt, struct sumjob *job)
{
	hist_record(&clnt->send_histogram, &job->send.begin);
}

static void
sumclnt_record_recv_delay(struct sumclnt *clnt, struct sumjob *job)
{
	hist_record(&clnt->recv_histogram, &job->recv.begin);
}

static int
sumjob_connect(struct sumclnt *clnt, struct sumjob *job)
{
	if (job->fd >= 0)
		return 0;

	job->fd = socket(PF_INET, SOCK_STREAM, 0);
	if (job->fd < 0) {
		perror("socket");
		return -1;
	}

	if (BASE_PORT != 0) {
		struct sockaddr_in myaddr;
		int on = 1;

		setsockopt(job->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		memset(&myaddr, 0, sizeof(myaddr));
		myaddr.sin_port = htons(BASE_PORT + job->id);

		if (bind(job->fd, (struct sockaddr *) &myaddr, sizeof(myaddr)) < 0) {
			perror("bind");
			return -1;
		}
	}

	/* Set NDELAY for non-blocking connect */
	fcntl(job->fd, F_SETFL, O_NDELAY);

	if (connect(job->fd, (struct sockaddr *) &clnt->svc_addr, clnt->svc_addrlen) >= 0) {
		job->last_activity = 'C';
	} else
	if (errno == EINPROGRESS) {
		job->last_activity = 'c';
	} else {
		perror("connect");
		return -1;
	}

	fcntl(job->fd, F_SETFL, 0);
	return 0;
}

static void
sumjob_close(struct sumjob *job)
{
	if (job->fd >= 0) {
		close(job->fd);
		job->fd = -1;
	}
}

static int
sumjob_send(struct sumclnt *clnt, struct sumjob *job)
{
	unsigned int nbytes, avail;
	int rv;

	if (job->proto != IPPROTO_TCP) {
		fprintf(stderr, "%s: protocol not supported\n", __func__);
		return -1;
	}

	if (job->fd < 0) {
		fprintf(stderr, "%s: not connected\n", __func__);
		return -1;
	}

	avail = job->send.len - job->send.pos;
	nbytes = random() % job->send.len;
	if (nbytes == 0)
		nbytes = 1;
	else if (nbytes > avail)
		nbytes = avail;

	rv = send(job->fd, job->send.buf + job->send.pos, nbytes, MSG_DONTWAIT);
	if (rv < 0) {
		perror("sendmsg");
		return -1;
	}

	job->last_activity = 'x';
	job->send.pos += rv;
	nbytes = rv;

	if (job->send.pos >= job->send.len) {
		/* We sent everything */
		job->last_activity = 'X';

		sumclnt_record_send_delay(clnt, job);
		gettimeofday(&job->recv.begin, NULL);
	}

	return 0;
}

static int
sumjob_recv(struct sumclnt *clnt, struct sumjob *job)
{
	unsigned int want;
	int rv;

	if (job->recv.buf == NULL) {
		job->recv.buf = malloc(1024);
		job->recv.size = 1024;

		if (job->proto == IPPROTO_TCP) {
			job->recv.len = 4; /* receive record marker first */
		} else {
			job->recv.len = job->recv.size;
		}
	}

	want = job->recv.len - job->recv.pos;
	rv = recv(job->fd, job->recv.buf + job->recv.pos, want, MSG_DONTWAIT);
	if (rv == 0) {
		log_error("%s: unexpected end of file on socket", __func__);
		return -1;
	}
	if (rv < 0) {
		log_error("%s: recv error on socket: %m", __func__);
		return -1;
	}

	job->last_activity = 'r';
	job->recv.pos += rv;
	if (job->proto == IPPROTO_TCP && job->recv.pos == 4 && job->recv.len == 4) {
		uint32_t marker;

		/* We received the record marker */
		memcpy(&marker, job->recv.buf, 4);
		marker = ntohl(marker);
		if (!(marker & 0x80000000))
			log_fatal("%s: TCP record without last-record marker", __func__);
		marker &= 0x7fffffff;

		if (marker < 16)
			log_fatal("%s: short RPC record from server (%u bytes)", __func__, marker);
		job->recv.len = marker;
		job->recv.pos = 0;
	}

	if (job->recv.pos >= job->recv.len) {
		/* We've received the entire message */
		if (sumjob_check_reply(job) < 0)
			log_fatal("%s: bad reply from server", __func__);

		sumclnt_record_recv_delay(clnt, job);
		job->last_activity = 'R';
		job->ncalls++;
		clnt->ncalls++;

		if (job->ncalls < job->max_calls) {
			sumjob_drop_buffers(job);
			if (!sumjob_build_packet(job) < 0)
				log_fatal("Failed to rebuild packet");
		} else {
			job->last_activity = '@';
			sumjob_close(job);
		}
	}

	return 0;
}

int
sumjob_check_reply(struct sumjob *job)
{
	struct rpc_msg msg;
	u_int32_t sum = 12345678;
	XDR xdrs;
	int rv = -1;

	memset(&msg, 0, sizeof(msg));

	msg.rm_reply.rp_acpt.ar_results.where = (caddr_t) &sum;
	msg.rm_reply.rp_acpt.ar_results.proc = (xdrproc_t) xdr_u_int;

	xdrmem_create(&xdrs, (char *) job->recv.buf, job->recv.len, XDR_DECODE);
	if (!xdr_replymsg(&xdrs, &msg)) {
		log_error("Cannot decode reply message");
		goto failed;
	}

	if (msg.rm_xid != job->xid) {
		log_error("Reply XID doesn't match (expect 0x%08x, got 0x%08x)", msg.rm_xid, job->xid);
		goto failed;
	}
	if (msg.rm_direction != REPLY) {
		log_error("Reply has invalid direction %d", msg.rm_direction);
		goto failed;
	}
	if (msg.rm_reply.rp_stat != MSG_ACCEPTED) {
		log_error("Reply has invalid rp_stat %d", msg.rm_reply.rp_stat);
		goto failed;
	}
	if (msg.rm_reply.rp_acpt.ar_stat != SUCCESS) {
		log_error("Remote RPC error %d", msg.rm_reply.rp_acpt.ar_stat);
		goto failed;
	}

	if (sum != job->sum) {
		log_error("Reply has wrong sum (expect %u, got %u)", job->sum, sum);
		goto failed;
	}

	rv = 0;

failed:
	xdr_free((xdrproc_t) xdr_callmsg, &msg);
	xdr_destroy(&xdrs);
	return rv;
}

static void
__sumjob_set_timeout(struct timeval *deadline, unsigned long timeout_usec)
{
	struct timeval timeout_tv;

	timeout_tv.tv_sec = timeout_usec / 1000000;
	timeout_tv.tv_usec = timeout_usec % 1000000;

	gettimeofday(deadline, NULL);
	timeradd(deadline, &timeout_tv, deadline);
}

static void
sumjob_set_timeout(struct sumclnt *clnt, struct sumjob *job)
{
	__sumjob_set_timeout(&job->timeout, clnt->job_timeout * 1000000);
}

struct sumjob *
sumjob_new(struct sumclnt *clnt, unsigned int jobid, unsigned int num_ints)
{
	static char namebuf[128];
	struct sumjob *job = calloc(1, sizeof(*job));

	snprintf(namebuf, sizeof(namebuf), "job%u", jobid);
	job->name = strdup(namebuf);
	job->id = jobid;

	job->max_calls = random() % clnt->max_calls;
	job->num_ints = num_ints;

	gettimeofday(&job->ctime, NULL);

	/* For now, only do TCP */
	job->proto = IPPROTO_TCP;
	job->fd = -1;

	job->xid = xid;
	job->xid += job->max_calls;

	if (sumjob_build_packet(job) < 0) {
		sumjob_free(job);
		return NULL;
	}

	return job;
}

static int
sumjob_build_packet(struct sumjob *job)
{
	unsigned int *input = NULL;
	struct rpc_msg msg;
	struct foodata args;
	unsigned int i;
	uint32_t marker = 0;
	XDR xdrs;
	int rv = -1;

	job->send.size = 128 + 4 * job->num_ints;
	job->send.buf = malloc(job->send.size);

	xdrmem_create(&xdrs, (char *) job->send.buf, job->send.size, XDR_ENCODE);

	/* If this is a stream, encode the record marker first */
	if (job->proto == IPPROTO_TCP
	 && !xdr_u_int(&xdrs, &marker))
		goto out;

	/* Serialize the RPC header first */
	memset(&msg, 0, sizeof(msg));
	msg.rm_xid = job->xid;
	msg.rm_direction = CALL;
	msg.rm_call.cb_rpcvers = 2;
	msg.rm_call.cb_prog = SQUARE_PROG;
	msg.rm_call.cb_vers = SQUARE_VERS;
	msg.rm_call.cb_proc = SUMPROC;
	if (!xdr_callmsg(&xdrs, &msg)) {
		log_error("failed to encode rpc message");
		goto out;
	}

	input = calloc(job->num_ints, sizeof(input[0]));
	for (i = 0, job->sum = 0; i < job->num_ints; ++i) {
		input[i] = random();
		job->sum += input[i];
	}

	memset(&args, 0, sizeof(args));
	args.buffer.buffer_val = input;
	args.buffer.buffer_len = job->num_ints;

	if (!xdr_foodata(&xdrs, &args))
		goto out;

	job->send.len = xdr_getpos(&xdrs);

	/* Update the record marker */
	if (job->proto == IPPROTO_TCP) {
		marker = htonl(0x80000000 | (job->send.len - 4));
		memcpy(job->send.buf, &marker, 4);
	}

	if (0) {
		unsigned int i = job->send.len;

		for (i = 0; i < job->send.len && i < 64; ++i) {
			if ((i % 16) == 0) {
				printf("\n%04x:", i);
			}
			printf(" %02x", job->send.buf[i]);
		}

		printf("\n");
	}

	rv = 0;

	/* Count xmit time from the point where we built the
	 * packet */
	gettimeofday(&job->send.begin, NULL);

out:
	xdr_destroy(&xdrs);

	if (input)
		free(input);

	return rv;
}

static void
sumjob_timeout(struct sumjob *job)
{
	log_error("%s: timed out while waiting for reply", job->name);
	sumjob_print(job);
	sumjob_close(job);
}

static const char *
__pollflags(int f)
{
	static struct flagname {
		int value;
		const char *name;
	} flag_names[] = {
		{ POLLIN, "in" },
		{ POLLOUT, "out" },
		{ POLLHUP, "hup" },
		{ POLLERR, "err" },
		{ POLLPRI, "pri" },
		{ 0 }
	};
	struct flagname *fl;
	static char strbuf[128];

	if (!f)
		return "";

	strbuf[0] = '\0';
	for (fl = flag_names; fl->value; ++fl) {
		if (!(fl->value & f))
			continue;

		if (strbuf[0])
			strcat(strbuf, "|");
		strcat(strbuf, fl->name);
		f &= ~fl->value;
	}
	if (fl->value) {
		if (strbuf[0])
			strcat(strbuf, "|");
		sprintf(strbuf + strlen(strbuf), "%02x", f);
	}
	return strbuf;
}

static void
sumjob_print(const struct sumjob *job)
{
	printf("Job %s: fd=%d send=<buf=%p,len=%u,pos=%u> recv=<buf=%p,len=%u,pos=%u>\n",
	       job->name, job->fd,
	       job->send.buf, job->send.len, job->send.pos,
	       job->recv.buf, job->recv.len, job->recv.pos);
	if (job->pollfd) {
		printf("  poll events=<%s>", __pollflags(job->pollfd->events));
		printf(" revents=<%s>\n", __pollflags(job->pollfd->revents));
	}
	fflush(stdout);
}

static void
sumjob_drop_buffers(struct sumjob *job)
{
	if (job->send.buf)
		free(job->send.buf);
	if (job->recv.buf)
		free(job->recv.buf);
	memset(&job->send, 0, sizeof(job->send));
	memset(&job->recv, 0, sizeof(job->recv));
}

static void
sumjob_free(struct sumjob *job)
{
	fflush(stdout);

	sumjob_drop_buffers(job);

	sumjob_close(job);

	if (job->name)
		free(job->name);
	job->name = NULL;

	free(job);
}

/*
 * Helper functions for handling timeouts
 */
static void
timeout_init(struct timeout *tmo, long initial_msec)
{
	memset(tmo, 0, sizeof(*tmo));

	gettimeofday(&tmo->now, NULL);
	tmo->current = -1;
	if (initial_msec >= 0)
		tmo->current = initial_msec;
}

static int
timeout_update(struct timeout *tmo, const struct timeval *expire)
{
	struct timeval delta;
	long value;

	if (timercmp(expire, &tmo->now, <=))
		return -1; /* expiry is in the past, return negative */

	timersub(expire,  &tmo->now, &delta);
	value = 1000 * delta.tv_sec + delta.tv_usec / 1000;

	/* Below granularity of poll() timeout */
	if (value == 0)
		return -1;

	tmo->current = value;
	return value;
}

static long
timeout_value(const struct timeout *tmo)
{
	return tmo->current;
}

static void
hist_init(struct histogram *h, double xrange)
{
	memset(h, 0, sizeof(*h));
	h->xrange = xrange;
	h->xscale = HIST_MAX * 1 / xrange;
}

static void
hist_record(struct histogram *h, const struct timeval *t0)
{
	struct timeval now, delta;
	double delay;
	unsigned int idx;

	gettimeofday(&now, NULL);
	if (timercmp(t0, &now, >)) {
		/* send time in the future?! */
		delay = h->xrange;
	} else {
		timersub(&now, t0, &delta);
		delay = delta.tv_sec + 1e-6 * delta.tv_usec;
	}

	idx = HIST_MAX * (delay * h->xscale);
	if (idx >= HIST_MAX)
		idx = HIST_MAX - 1;
	h->values[idx]++;
}

static void
hist_print(struct histogram *h, unsigned int nlines)
{
	double scaled[HIST_MAX], f;
	unsigned int max_value = 0;
	unsigned int i;
	int scanline;

	for (i = 0; i < HIST_MAX; ++i) {
		if (h->values[i] >= max_value)
			max_value = h->values[i];
	}
	if (max_value == 0)
		return;
	f = 1.0 * nlines / max_value;
	for (i = 0; i < HIST_MAX; ++i)
		scaled[i] = h->values[i] * f;

	for (scanline = nlines; scanline >= 0; --scanline) {
		for (i = 0; i < HIST_MAX; ++i) {
			double v = scaled[i] - scanline;
			if (v > 0.5)
				putc('#', stdout);
			else if (v > 0)
				putc('.', stdout);
			else
				putc(' ', stdout);
		}
		printf("| %u\n", (unsigned int) (scanline / f));
	}
	for (i = 0; i < HIST_MAX; i += 10)
		printf("+---------");
	printf("+\n");
	for (i = 0; i < HIST_MAX; i += 10)
		printf("%-10u", (int) (i * h->xrange / HIST_MAX * 1000));
	printf("%u msec\n", (int) (i * h->xrange / HIST_MAX * 1000));
}

