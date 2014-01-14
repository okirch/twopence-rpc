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
#include <assert.h>
#include "rpctest.h"

static int		opt_log_quiet;
static int		opt_log_testbus;
static char		test_group_msg[1024];
static char		test_msg[1024];

static unsigned int	test_group_index;
static const char *	test_root_name;
static const char *	test_group_name;
static const char *	test_case_name;

unsigned int		num_tests;
unsigned int		num_warns;
unsigned int		num_fails;

void
log_quiet(void)
{
	opt_log_quiet = 1;
}

void
log_format_testbus(const char *prefix)
{
	test_root_name = prefix;
	opt_log_testbus = 1;
}

enum {
	TEST_BEGIN_GROUP, TEST_BEGIN, TEST_SUCCESS, TEST_FAILURE, TEST_WARNING,
};

static void
__log_test_begin_or_end(int type, const char *name, const char *extra_fmt, va_list extra_ap)
{
	if (opt_log_testbus) {
		switch (type) {
		case TEST_BEGIN_GROUP:
		case TEST_BEGIN:
			fprintf(stderr, "### TESTBEGIN %s", name);
			break;

		case TEST_SUCCESS:
			fprintf(stderr, "### TESTRESULT %s: SUCCESS", name);
			break;

		case TEST_FAILURE:
			fprintf(stderr, "### TESTRESULT %s: FAILED", name);
			break;

		case TEST_WARNING:
			fprintf(stderr, "### TESTRESULT %s: WARN", name);
			break;

		default:
			fprintf(stderr, "### INTERNALERROR");
			break;
		}

		if (extra_fmt) {
			fprintf(stderr, " (");
			vfprintf(stderr, extra_fmt, extra_ap);
			fprintf(stderr, ")");
		}
		fprintf(stderr, "\n");
	} else {
		switch (type) {
		case TEST_BEGIN_GROUP:
			fprintf(stderr, "=== ");
			if (extra_fmt)
				vfprintf(stderr, extra_fmt, extra_ap);
			fprintf(stderr, " === \n");
			break;

		case TEST_BEGIN:
			fprintf(stderr, "TEST: ");
			if (extra_fmt)
				vfprintf(stderr, extra_fmt, extra_ap);
			fprintf(stderr, "\n");
			break;

		case TEST_SUCCESS:
			/* Nothing */
			return;

		case TEST_FAILURE:
			fprintf(stderr, "FAIL: ");
			if (extra_fmt)
				vfprintf(stderr, extra_fmt, extra_ap);
			fprintf(stderr, "\n");
			break;

		case TEST_WARNING:
			fprintf(stderr, "WARN: ");
			if (extra_fmt)
				vfprintf(stderr, extra_fmt, extra_ap);
			fprintf(stderr, "\n");
			break;
		}

	}
}

static void
__log_test_finish(const char **namep)
{
	if (*namep != NULL) {
		__log_test_begin_or_end(TEST_SUCCESS, *namep, NULL, NULL);
		*namep = NULL;
	}
}

void
log_test_group(const char *groupname, const char *fmt, ...)
{
	va_list ap;

	__log_test_finish(&test_case_name);
	__log_test_finish(&test_group_name);

	va_start(ap, fmt);
	if (!opt_log_quiet) {
		__log_test_begin_or_end(TEST_BEGIN_GROUP, groupname, fmt, ap);
	} else {
		vsnprintf(test_group_msg, sizeof(test_group_msg), fmt, ap);
	}

	test_group_name = groupname;
	test_group_index = 0;
	va_end(ap);
}

static void
__log_test_tagged(const char *tag, const char *fmt, va_list ap)
{
	static char *namebuf = NULL;
	const char *level[5];
	unsigned int nlevels = 0;

	__log_test_finish(&test_case_name);

	if (test_root_name)
		level[nlevels++] = test_root_name;
	if (test_group_name)
		level[nlevels++] = test_group_name;
	if (tag)
		level[nlevels++] = tag;

	if (nlevels > 1) {
		unsigned int i = 0, len, off;

		for (len = i = 0; i < nlevels; ++i)
			len += strlen(level[i]) + 1;

		namebuf = realloc(namebuf, len);

		for (off = i = 0; i < nlevels; ++i) {
			if (i)
				namebuf[off++] = '.';
			strcpy(namebuf + off, level[i]);
			off += strlen(namebuf + off);
		}
		assert(off < len);

		test_case_name = namebuf;
	} else {
		test_case_name = tag;
	}

	if (!opt_log_quiet) {
		__log_test_begin_or_end(TEST_BEGIN, test_case_name, fmt, ap);
	} else {
		vsnprintf(test_msg, sizeof(test_msg), fmt, ap);
	}

	test_group_index++;
	num_tests++;
}

void
log_test_tagged(const char *tag, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__log_test_tagged(tag, fmt, ap);
	va_end(ap);
}

void
log_test(const char *fmt, ...)
{
	char tagname[32];
	va_list ap;

	va_start(ap, fmt);
	snprintf(tagname, sizeof(tagname), "testcase%u", test_group_index);
	__log_test_tagged(tagname, fmt, ap);
	va_end(ap);
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
	__log_test_begin_or_end(TEST_WARNING, test_case_name, fmt, ap);
	va_end(ap);

	num_warns++;
}

void
log_fail(const char *fmt, ...)
{
	va_list ap;

	__log_msg_flush();

	if (test_case_name) {
		va_start(ap, fmt);
		__log_test_begin_or_end(TEST_FAILURE, test_case_name, fmt, ap);
		va_end(ap);

		test_case_name = NULL;
	}

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
