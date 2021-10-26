#include "util.h"
#include "error.h"
#include "snapcast.h"

#include <time.h>

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>

#define STRBUFELEMENTS 4
static char strbuffer[STRBUFELEMENTS][INET6_ADDRSTRLEN + 1];
static int str_bufferoffset = 0;

int max(const int a, const int b) {
	if (a > b)
		return a;
	return b;
}

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

	if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event) == -1)
		exit_errno("error on add_fd %d on efd %d, epoll_ctl (ADD)", fd, efd);
}

void del_fd(int efd, int fd) {
	if (epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL)  == -1)
		exit_errno("error on del_fd %d on efd %d, epoll_ctl(DEL)", fd, efd);
}

const char *print_mac(const uint8_t mac[6]) {
	str_bufferoffset = (str_bufferoffset + 1) % STRBUFELEMENTS;
	snprintf(strbuffer[str_bufferoffset], 18, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return strbuffer[str_bufferoffset];
}

const char *print_codec(int codec) {
	str_bufferoffset = (str_bufferoffset + 1) % STRBUFELEMENTS;
	if (codec == PCM)
		snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "pcm");
	else if (codec == OPUS)
		snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "opus");
	else
		snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "unknown");
	return strbuffer[str_bufferoffset];
}

const char *print_stream_protocol(int protocol) {
	str_bufferoffset = (str_bufferoffset + 1) % STRBUFELEMENTS;
	if (protocol == PIPE)
		snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "pipe");
	else
		snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "unknown");
	return strbuffer[str_bufferoffset];
}

const char *print_inputpipe_status(int status) {
	str_bufferoffset = (str_bufferoffset + 1) % STRBUFELEMENTS;
	if (status == IDLE)
		snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "idle");
	else if (status == PLAYING)
		snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "playing");
	else if (status == THROTTLE)
		snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "throttle");
	else
		snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "unknown");

	return strbuffer[str_bufferoffset];
}

const char *print_chunk(pcmChunk *chunk) {
	str_bufferoffset = (str_bufferoffset + 1) % STRBUFELEMENTS;
	snprintf(strbuffer[str_bufferoffset], INET6_ADDRSTRLEN, "codec: %i play_at: %u.%09u", chunk->codec, chunk->play_at_tv_sec,
		 chunk->play_at_tv_nsec);
	return strbuffer[str_bufferoffset];
}

void print_packet(unsigned char *buf, int size) {
	if (!snapctx.debug)
		return;

	log_debug("Packet: [");
	for (int i = 0; i < size; i++) {
		if (i % 4 == 0)
			log_debug(" ");
		log_debug("%02hhX", buf[i]);
	}
	log_debug("]\n");
}
