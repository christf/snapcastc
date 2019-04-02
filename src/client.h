#pragma once

#include "taskqueue.h"
#include "vector.h"

#include <netdb.h>
#include <netinet/in.h>

typedef struct client {
	struct in6_addr ip;
	uint16_t port;
	char name[64];
	char hostname[NI_MAXHOST];
	uint32_t id;
	int group;		   // TODO: implement status
	bool connected;		   // TODO implement
	struct timespec lastseen;  // TODO implement
	char version[10];	  // TODO implement
	uint8_t protoversion;
	uint8_t mac[6];  // TODO implement

	uint32_t latency;

	uint8_t volume_percent;
	bool muted;

	taskqueue_t *purge_task;
} client_t;

typedef VECTOR(struct client) client_vector;

