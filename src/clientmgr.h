#pragma once

#include <netdb.h>
#include <netinet/in.h>
#include <sys/time.h>
#include "taskqueue.h"
#include "vector.h"

typedef VECTOR(struct client) client_vector;

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

struct client_purge_task {
	uint16_t packet_len;
	struct client *client;
	uint8_t *packet;
	taskqueue_t *check_task;
};

typedef struct {
	struct snapctx *ctx;
	client_vector clients;
	struct snapctx *snapctx;
} clientmgr_ctx;

client_t *new_client(client_t *ret, const uint32_t id, const struct in6_addr *host, const uint16_t port);
struct client *get_client(const uint32_t clientid);

void print_client(struct client *client);
bool clientmgr_refresh_client(struct client *client);
void clientmgr_purge_clients(clientmgr_ctx *ctx);
void clientmgr_delete_client(clientmgr_ctx *ctx, const uint32_t clientid);
void clientmgr_stop_clients();
bool clientmgr_client_setmute(uint32_t clientid, bool mute);
void clientmgr_init();
