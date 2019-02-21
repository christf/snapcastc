#pragma once
#include "pcmchunk.h"

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>

#define INTERCOM_PORT 1704

void log_verbose(const char *format, ...);
void log_debug(const char *format, ...);
void log_error(const char *format, ...);
const char *print_ip(const struct in6_addr *addr);
const char *print_timespec(const struct timespec *t);
void print_packet(unsigned char *buf, int size);
const char *print_mac(const uint8_t mac[6]);

void add_fd(int efd, int fd, uint32_t events);
void del_fd(int efd, int fd);
const char *print_chunk(pcmChunk *chunk);
