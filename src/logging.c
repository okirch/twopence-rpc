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
 * Logging functions; internal use only
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "rpctest.h"

static int		opt_log_quiet;
static char		test_group_msg[1024];
static char		test_msg[1024];

unsigned int		num_tests;
unsigned int		num_warns;
unsigned int		num_fails;

void
log_quiet(void)
{
	opt_log_quiet = 1;
}

void
log_test_group(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (!opt_log_quiet) {
		fprintf(stderr, "=== ");
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, " === \n");
	} else {
		vsnprintf(test_group_msg, sizeof(test_group_msg), fmt, ap);
	}
	va_end(ap);
}

void
log_test(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (!opt_log_quiet) {
		fprintf(stderr, "TEST: ");
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	} else {
		vsnprintf(test_msg, sizeof(test_msg), fmt, ap);
	}
	va_end(ap);

	num_tests++;
}

static void
__log_msg_flush(void)
{
	if (test_group_msg[0]) {
		fprintf(stderr, "== %s ==\n", test_group_msg);
		test_group_msg[0] = '\0';
	}
	if (test_msg[0]) {
		fprintf(stderr, "TEST: %s\n", test_msg);
		test_msg[0] = '\0';
	}
}

void
log_warn(const char *fmt, ...)
{
	va_list ap;

	__log_msg_flush();

	va_start(ap, fmt);
	fprintf(stderr, "WARN: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);

	num_warns++;
}

void
log_fail(const char *fmt, ...)
{
	va_list ap;

	__log_msg_flush();

	va_start(ap, fmt);
	fprintf(stderr, "FAIL: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);

	num_fails++;
}

void
log_error(const char *fmt, ...)
{
	va_list ap;

	__log_msg_flush();

	va_start(ap, fmt);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

void
log_trace(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "::: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

void
log_fatal(const char *fmt, ...)
{
	va_list ap;

	__log_msg_flush();

	va_start(ap, fmt);
	fprintf(stderr, "FATAL ERROR: *** ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, " ***\n");
	va_end(ap);

	exit(1);
}
