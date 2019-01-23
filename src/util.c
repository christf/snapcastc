#include "util.h"
#include "error.h"
#include "snapcast.h"

#include <time.h>

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>

#define STRBUFELEMENTS 3
static char strbuffer[STRBUFELEMENTS][INET6_ADDRSTRLEN + 1];
static int str_bufferoffset = 0;

/* print a human-readable representation of an in6_addr struct to string buffer which can then be printed to wherever
** ** */
const char *print_ip(const struct in6_addr *addr) {
	str_bufferoffset = (str_bufferoffset + 1) % STRBUFELEMENTS;
	return inet_ntop(AF_INET6, &(addr->s6_addr), strbuffer[str_bufferoffset], INET6_ADDRSTRLEN);
}

/** print a timespec
*/
const char *print_timespec(const struct timespec *t) {
	str_bufferoffset = (str_bufferoffset + 1) % STRBUFELEMENTS;
	snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "%lu.%09lu", t->tv_sec, t->tv_nsec);
	return strbuffer[str_bufferoffset];
}

void log_error(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

void log_debug(const char *format, ...) {
	if (!snapctx.debug)
		return;
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

void log_verbose(const char *format, ...) {
	if (!snapctx.verbose)
		return;
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

void add_fd(int efd, int fd, uint32_t events) {
	log_debug("watching fd %d using epoll\n", fd);
	struct epoll_event event = {};
	event.data.fd = fd;
	event.events = events;

	int s = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event);
	if (s == -1) {
		perror("epoll_ctl (ADD):");
		exit_error("epoll_ctl");
	}
}

void del_fd(int efd, int fd) {
	int s = epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
	if (s == -1) {
		perror("epoll_ctl (DEL):");
		exit_error("epoll_ctl");
	}
}

void print_packet(unsigned char *buf, int size) {
	log_debug("Packet: [");
	for (int i = 0; i < size; i++) {
		if (i % 4 == 0)
			log_debug(" ");
		log_debug("%02hhX", buf[i]);
	}
	log_debug("]\n");
}
