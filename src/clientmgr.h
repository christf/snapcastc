#pragma once

#include <netinet/in.h>
#include <sys/time.h>
#include "taskqueue.h"
#include "vector.h"

typedef VECTOR(struct client) client_vector;

typedef struct client {
	struct timespec lastseen;
	struct in6_addr ip;
	int fd;
	uint16_t port;
	char *name;
	uint32_t id;

	taskqueue_t *purge_task;
} client_t;

struct client_purge_task {
	uint16_t packet_len;
	struct client *client;
	uint8_t *packet;
	taskqueue_t *check_task;
};

typedef struct {
	struct snapctx *ctx;
	client_vector clients;
	int client_timeout;  // purge client when not receiving a "hello" for CLIENT_TIMEOUT seconds
	struct snapctx *snapctx;
} clientmgr_ctx;

client_t new_client(const uint32_t id, const struct in6_addr *host, const uint16_t port);
struct client *get_client(const uint32_t clientid);

void free_client(client_t *client);

void print_client(struct client *client);
bool clientmgr_refresh_client(struct client *client);
void clientmgr_purge_clients(clientmgr_ctx *ctx);
void clientmgr_delete_client(clientmgr_ctx *ctx, const uint32_t clientid);
void clientmgr_init();
