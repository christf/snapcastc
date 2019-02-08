#include "clientmgr.h"
#include "alloc.h"
#include "intercom.h"
#include "snapcast.h"
#include "syscallwrappers.h"
#include "util.h"

#include <netdb.h>
#include <string.h>

/** Given a client name returns a client object.
**   Returns NULL if the client is not known.
**/
struct client *findinvector(void *_vector, const uint32_t id) {
	VECTOR(struct client) *vector = _vector;
	for (int _vector_index = VECTOR_LEN(*vector) - 1; _vector_index >= 0; _vector_index--) {
		struct client *e = &VECTOR_INDEX(*vector, _vector_index);

		if (id == e->id)
			return e;
	}

	return NULL;
}

void clientmgr_stop_clients() {
	for (int i = VECTOR_LEN(snapctx.clientmgr_ctx.clients) - 1; i >= 0; i--) {
		struct client *c = &VECTOR_INDEX(snapctx.clientmgr_ctx.clients, i);
		intercom_stop_client(&snapctx.intercom_ctx, &c->ip, c->port);
	}
}

void schedule_delete_client(void *d) {
	uint32_t *id = d;
	clientmgr_delete_client(&snapctx.clientmgr_ctx, *id);
}

client_t *new_client(client_t *ret, const uint32_t id, const struct in6_addr *ip, const uint16_t port) {
	struct sockaddr_in6 speer = {};

	memcpy(&speer.sin6_addr, ip, sizeof(struct in6_addr));
	speer.sin6_port = port;
	speer.sin6_family = AF_INET6;

	ret->name[0] = '\0';
	int s = getnameinfo((struct sockaddr *)&speer, 128, ret->name, NI_MAXHOST, NULL, 0, NI_NOFQDN);
	if (s != 0)
		log_error("getnameinfo: %s\n", gai_strerror(s));
	else
		log_verbose("hostname of peer: %s\n", ret->name);

	ret->port = port;
	memcpy(&ret->ip, ip, sizeof(struct in6_addr));
	ret->id = id;

	uint32_t *cid = snap_alloc(sizeof(uint32_t));
	*cid = id;
	ret->purge_task = post_task(&snapctx.taskqueue_ctx, 5, 0, schedule_delete_client, free, cid);

	VECTOR_ADD(snapctx.clientmgr_ctx.clients, *ret);

	return ret;
}

void free_client_members(client_t *client) {  // free(client->name);
}

struct client *get_client(const uint32_t id) {
	return findinvector(&snapctx.clientmgr_ctx.clients, id);
}

void print_client(struct client *client) {
	log_verbose("Client %s(%u) has IP %s, port %i\n", client->name, client->id, print_ip(&client->ip), client->port);
}

/** Given a MAC address deletes a client. Safe to call if the client is not
**   known.
**     */
void clientmgr_delete_client(clientmgr_ctx *ctx, const uint32_t id) {
	// TODO: do not parse the vector twice to get the client once and the index on the second run.
	struct client *client = get_client(id);

	if (client == NULL) {
		log_debug("Client [%lu] unknown: cannot delete\n", id);
		return;
	}

	log_verbose("\033[34mREMOVING client %lu\033[0m\n", id);
	print_client(client);

	free_client_members(client);

	// TODO migrate to vector_lsearch
	for (int i = VECTOR_LEN(ctx->clients) - 1; i >= 0; i--) {
		if (VECTOR_INDEX(ctx->clients, i).id == id) {
			VECTOR_DELETE(ctx->clients, i);
			break;
		}
	}
}

/** Remove all client routes - used when exiting
** **/
void clientmgr_purge_clients(clientmgr_ctx *ctx) {
	struct client *client;

	for (int i = VECTOR_LEN(ctx->clients) - 1; i >= 0; i--) {
		client = &VECTOR_INDEX(ctx->clients, i);
		clientmgr_delete_client(ctx, client->id);
	}
}

bool clientmgr_refresh_client(struct client *client) {
	client_t *existingclient = get_client(client->id);
	struct timespec ctime;
	obtainsystime(&ctime);

	if (!existingclient) {
		// create new client
		log_verbose("clientmgr: creating client: %lu\n", client->id);
		client_t n_client = {};
		new_client(&n_client, client->id, &client->ip, client->port);
		existingclient = get_client(client->id);

		for (int i = 0; i < VECTOR_LEN(snapctx.intercom_ctx.packet_buffer); ++i) {
			log_debug("sending packet %d\n", i);
			audio_packet *ap = &VECTOR_INDEX(snapctx.intercom_ctx.packet_buffer, i);
			intercom_send_packet_unicast(&snapctx.intercom_ctx, &existingclient->ip, ap->data, ap->len, existingclient->port);
		}

		existingclient->protoversion = 2;  // For some reason we have protoversion 2 for clients now.

		// TODO: remove this hack and use actual mac address
		existingclient->mac[0] = 0xff;
		existingclient->mac[1] = 0xff;
		memcpy(&existingclient->mac[2], &client->id, 4);
	}

	existingclient->connected = true;
	existingclient->lastseen = ctime;
	log_verbose("clientmgr: refreshing client: %lu\n", client->id);
	print_client(existingclient);
	reschedule_task(&snapctx.taskqueue_ctx, existingclient->purge_task, 5, 0);

	return true;
}

void clientmgr_init() { VECTOR_INIT((&snapctx.clientmgr_ctx)->clients); }
