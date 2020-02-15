#include "clientmgr.h"
#include "alloc.h"
#include "inputpipe.h"
#include "inputpipe.h"
#include "intercom_srv.h"
#include "packet_types.h"
#include "snapcast.h"
#include "stream.h"
#include "syscallwrappers.h"
#include "util.h"
#include "vector.h"

#include <netdb.h>
#include <string.h>

/** Given a client name returns a client object.
**   Returns NULL if the client is not known.
**/
void clientmgr_send_audio_buffer_to_client(client_t *client) {
	stream *s = stream_find(client);
	for (int i = 0; i < VECTOR_LEN(s->packet_buffer); ++i) {
		log_verbose("sending packet %d to %s\n", i, print_ip(&client->ip));
		audio_packet *ap = &VECTOR_INDEX(s->packet_buffer, i);
		intercom_send_packet_unicast(&snapctx.intercom_ctx, &client->ip, ap->data, ap->len, client->port);
	}
}

bool clientmgr_client_refreshvolume(client_t *client, uint8_t volume) { return intercom_set_volume(&snapctx.intercom_ctx, client, volume); }

bool clientmgr_client_setmute(client_t *c, bool mute) {
	bool mute_changed = false;
	if (c->muted != mute) {
		mute_changed = true;
		c->muted = mute;
	}

	if (mute)
		intercom_stop_client(&snapctx.intercom_ctx, c);
	else if ((!mute) && mute_changed)
		clientmgr_send_audio_buffer_to_client(c);

	return true;
}

void clientmgr_stop_clients(stream *s) {
	for (int i = VECTOR_LEN(s->clients) - 1; i >= 0; i--) {
		struct client *c = &VECTOR_INDEX(s->clients, i);
		intercom_stop_client(&snapctx.intercom_ctx, c);
	}
}

void schedule_delete_client(void *d) {
	struct delete_client_data *data = (struct delete_client_data *)d;
	clientmgr_delete_client(data->id);
}

client_t *new_client(client_t *ret, const uint32_t id, const struct in6_addr *ip, const uint16_t port) {
	struct sockaddr_in6 speer = {};

	log_verbose("\033[34mADDING client %lu\033[0m\n", id);

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

	struct delete_client_data *data = snap_alloc(sizeof(struct delete_client_data));
	data->id = id;

	ret->purge_task = post_task(&snapctx.taskqueue_ctx, 5, 0, schedule_delete_client, free, data);


	return ret;
}

struct client *get_client(stream *s, const uint32_t id) {
	client_vector *vector = &s->clients;
	client_t key = {.id = id};
	return VECTOR_LSEARCH(&key, *vector, client_cmp);
}

void print_client(struct client *client) {
	log_verbose("Client %s(%u) has IP %s, port %i\n", client->name, client->id, print_ip(&client->ip), client->port);
}

void delete_client_internal(stream *s, client_t *c) {
	if (c && s) {
		log_verbose("\033[34mREMOVING client %lu, %d clients left in stream.\033[0m\n", c->id, VECTOR_LEN(s->clients) - 1);
		print_client(c);
		stream_client_remove(s, c);
	} else {
		log_error("Client [%lu] unknown: cannot delete\n", c ? c->id : 0);
	}
}

/** Given an id deletes a client. Safe to call if the client is not known.
*/
void clientmgr_delete_client(const uint32_t id) {
	client_stream cs = find_client(id);

	delete_client_internal(cs.stream, cs.client);
}
/** Remove all client routes - used when exiting
** **/
void clientmgr_purge_clients(stream *s) {
	struct client *client;

	for (int i = VECTOR_LEN(s->clients) - 1; i >= 0; i--) {
		client = &VECTOR_INDEX(s->clients, i);
		delete_client_internal(s, client);
	}
}

bool is_roughly(uint8_t a, uint8_t b) {
	if (a == b)
		return true;
	else {
		int sa = a;
		int sb = b;
		if (abs(sa - sb) < 2)
			return true;
	}

	return false;
}

client_stream find_client(const uint32_t id) {
	client_t *client = NULL;
	client_stream cs = {};

	for (int i = 0; i < VECTOR_LEN(snapctx.streams); ++i) {
		client = get_client(&VECTOR_INDEX(snapctx.streams, i), id);
		if (client) {
			cs.stream = &VECTOR_INDEX(snapctx.streams, i);
			cs.client = client;
			break;
		}
	}

	return cs;
}

bool clientmgr_refresh_client(struct client *client) {
	if (!client)
		return false;

	client_t *existingclient = find_client(client->id).client;

	struct timespec ctime;
	obtainsystime(&ctime);

	if (!existingclient) {
		// create new client
		log_verbose("clientmgr: creating client: %lu\n", client->id);
		client_t n_client = {};
		new_client(&n_client, client->id, &client->ip, client->port);


		stream *s = &VECTOR_INDEX(snapctx.streams, 0);

		if (!(s->initialized))
			stream_init(s);

		stream_client_add(s, &n_client);

		existingclient = get_client(s, client->id);
		existingclient->volume_percent = client->volume_percent;

		clientmgr_send_audio_buffer_to_client(existingclient);

		existingclient->protoversion = 2;  // For some reason we have protoversion 2 for clients now.

		// TODO: remove this hack and use actual mac address
		existingclient->mac[0] = 0xff;
		existingclient->mac[1] = 0xff;
		memcpy(&existingclient->mac[2], &client->id, 4);
	}

	existingclient->connected = true;
	existingclient->lastseen = ctime;
	existingclient->latency = client->latency;

	if (!is_roughly(client->volume_percent, existingclient->volume_percent)) {
		log_verbose("adjusting volume of client %d to recorded value of %d%%\n", client->volume_percent, existingclient->volume_percent);
		clientmgr_client_refreshvolume(existingclient, existingclient->volume_percent);
	}

	// It might be a different client instance, if the port changed. Let's let the old instance time out
	if (client->port == existingclient->port) {
		log_verbose("clientmgr: refreshing client: %lu\n", client->id);
		print_client(existingclient);
		reschedule_task(&snapctx.taskqueue_ctx, existingclient->purge_task, 5, 0);
	}

	return true;
}

void clientmgr_init() {}
