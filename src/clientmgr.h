#pragma once

#include <netdb.h>
#include <netinet/in.h>
#include <sys/time.h>
#include "stream.h"
#include "taskqueue.h"
#include "vector.h"

struct client_purge_task {
	uint16_t packet_len;
	struct client *client;
	uint8_t *packet;
	taskqueue_t *check_task;
};

typedef struct {
	client_t *client;
	stream *stream;
} client_stream;

struct delete_client_data {
	uint16_t id;
};

client_t *new_client(client_t *ret, const uint16_t id, const struct in6_addr *host, const uint16_t port);
struct client *get_client(stream *s, const uint16_t clientid);

client_stream find_client(const uint16_t id);

void print_client(struct client *client);
bool clientmgr_refresh_client(struct client *client);
void clientmgr_purge_clients(stream *s);
void clientmgr_delete_client(const uint16_t clientid);
void clientmgr_stop_clients(stream *s);
bool clientmgr_client_setmute(client_t *client, bool mute);
bool clientmgr_client_refreshvolume(client_t *client, uint8_t volume);
void clientmgr_init();
