#include "alloc.h"
#include "snapcast.h"
#include "intercom_common.h"
#include "util.h"
#include "timespec.h"
#include "syscallwrappers.h"
#include <error.h>

extern uint32_t nonce;

int parse_request(uint8_t *packet, uint32_t *nonce) {
	uint32_t tmp;
	memcpy(&tmp, &packet[2], sizeof(uint32_t));
	*nonce = ntohl(tmp);
	return packet[1];
}

int parse_hello(uint8_t *packet, client_t *client) {
	uint32_t tmp;
	memcpy(&tmp, &packet[2], sizeof(uint32_t));
	client->id = ntohl(tmp);
	memcpy(&tmp, &packet[6], sizeof(uint32_t));
	client->latency = ntohl(tmp);
	client->volume_percent = packet[10];
	return packet[1];
}

int assemble16(uint8_t *dst, uint16_t *src) {
	uint16_t tmp;
	tmp = htons(*src);
	memcpy(dst, &tmp, sizeof(uint16_t));
	return sizeof(uint16_t);
}

int assemble32(uint8_t *dst, uint32_t *src) {
	uint32_t tmp;
	tmp = htonl(*src);
	memcpy(dst, &tmp, sizeof(uint32_t));
	return sizeof(uint32_t);
}

void remove_old_audiodata_task(void *data) {
	stream *s = (stream *)data;
	struct timespec ctime, play_at;
	obtainsystime(&ctime);
	struct timespec age;

	if (!VECTOR_LEN(s->clients))
		return;

	do {
		audio_packet *ap = &VECTOR_INDEX(s->packet_buffer, 0);

		pcmChunk chunk;
		pcmChunk *pchunk = (pcmChunk *)&((uint8_t *)ap->data)[CHUNK_HEADER_SIZE + sizeof(intercom_packet_audio)];
		chunk_copy_meta(&chunk, pchunk);
		chunk_ntoh(&chunk);

		play_at.tv_sec = chunk.play_at_tv_sec;
		play_at.tv_nsec = chunk.play_at_tv_nsec;

		age = timeSubMs(&ctime, snapctx.bufferms);

		free(ap->data);
		VECTOR_DELETE(s->packet_buffer, 0);  // always remove the oldest element
	} while (timespec_cmp(age, play_at) > 0);
}


int assemble_volume(uint8_t *packet, uint32_t nonce, uint8_t volume) {
	packet[0] = CLIENT_VOLUME;
	packet[1] = 3;
	packet[2] = volume;
	return packet[1];
}

int assemble_stop(uint8_t *packet, uint32_t nonce) {
	packet[0] = CLIENT_STOP;
	packet[1] = 2;
	return packet[1];
}

bool intercom_set_volume(intercom_ctx *ctx, const client_t *client, uint8_t volume) {
	int packet_len = 0;
	uint8_t *packet = snap_alloc(sizeof(intercom_packet_op) + sizeof(tlv_op));

	stream *s = stream_find(client);
	packet_len = assemble_header(&((intercom_packet_op *)packet)->hdr, SERVER_OPERATION, &s->nonce, 0);
	packet_len += assemble_volume(&packet[packet_len], get_nonce(&s->nonce), volume);

	return intercom_send_packet_unicast(&snapctx.intercom_ctx, &client->ip, packet, packet_len, client->port);
}

bool intercom_stop_client(intercom_ctx *ctx, const client_t *client) {
	int packet_len = 0;
	uint8_t *packet = snap_alloc(sizeof(intercom_packet_op) + sizeof(tlv_op));

	stream *s = stream_find(client);
	packet_len = assemble_header(&((intercom_packet_op *)packet)->hdr, SERVER_OPERATION, &s->nonce, 0);
	packet_len += assemble_stop(&packet[packet_len], get_nonce(&s->nonce));

	return intercom_send_packet_unicast(&snapctx.intercom_ctx, &client->ip, packet, packet_len, client->port);
}

/** send chunk to all clients that are currently active
*/
void intercom_send_audio(intercom_ctx *ctx, stream *s) {
	pcmChunk *chunk = &(s->inputpipe.chunk);
	log_debug("sending %d Bytes of audio data\n", chunk->size);
	uint8_t packet[CHUNK_HEADER_SIZE + chunk->size];

	ssize_t packet_len;
	packet_len = assemble_header(&((intercom_packet_audio *)packet)->hdr, AUDIO_DATA, &s->nonce, 0);

	((intercom_packet_audio *)packet)->bufferms = htons(snapctx.bufferms);
	packet_len += sizeof(snapctx.bufferms);

	packet_len += assemble32(&packet[packet_len], &chunk->play_at_tv_sec);
	packet_len += assemble32(&packet[packet_len], &chunk->play_at_tv_nsec);
	packet_len += assemble32(&packet[packet_len], &chunk->samples);
	packet[packet_len++] = chunk->frame_size;
	packet[packet_len++] = chunk->channels;
	packet_len += assemble16(&packet[packet_len], &chunk->size);
	packet[packet_len++] = chunk->codec;

	memcpy(&packet[packet_len], chunk->data, chunk->size);
	packet_len += chunk->size;

	audio_packet ap;
	ap.data = snap_alloc(packet_len);
	memcpy(ap.data, packet, packet_len);
	ap.len = packet_len;
	ap.nonce = ntohl(((intercom_packet_audio *)packet)->hdr.nonce);
	VECTOR_ADD(s->packet_buffer, ap);

	// since we always write into this vector in ascending order, we can always remove the very first item on timeout and thus do not have
	// to pass the information which packet to remove
	post_task(&snapctx.taskqueue_ctx, (snapctx.bufferms / 1000), snapctx.bufferms % 1000, remove_old_audiodata_task, NULL, s);

	for (int i = VECTOR_LEN(s->clients) - 1; i >= 0; i--) {
		struct client *c = &VECTOR_INDEX(s->clients, i);
		if (!c->muted)
			intercom_send_packet_unicast(&snapctx.intercom_ctx, &c->ip, packet, packet_len, c->port);
	}
}


bool intercom_handle_client_operation(intercom_ctx *ctx, intercom_packet_op *packet, int packet_len, struct in6_addr *peer, uint16_t port) {
	/*
	** on hello: send stream info, add client to clientmgr
	** on request: resend packets with the referenced seqno
	** */
	log_debug("handling client operation for client %s port %d\n", print_ip(peer), port);

	int currentoffset = sizeof(intercom_packet_hdr);
	uint8_t type, *packetpointer;
	struct client client = {};
	uint32_t nonce;

	while (currentoffset < packet_len) {
		packetpointer = &((uint8_t *)packet)[currentoffset];
		type = *packetpointer;
		log_debug("offset: %i %p %p\n", currentoffset, packet, packetpointer);
		switch (type) {
			case HELLO:
				currentoffset += parse_hello(packetpointer, &client);
				memcpy(&client.ip, peer, sizeof(struct in6_addr));
				client.port = port;
				clientmgr_refresh_client(&client);
				break;
			case REQUEST:;
				stream *s = find_client(ntohs(packet->hdr.clientid)).stream;
				currentoffset += parse_request(packetpointer, &nonce);
				if (s) {
					audio_packet key = {.nonce = nonce};
					audio_packet *ap = VECTOR_LSEARCH(&key, s->packet_buffer, cmp_audiopacket);
					if (ap) {
						intercom_send_packet_unicast(ctx, peer, ap->data, ap->len, port);
						log_error("RE-SENT PACKET WITH id %lu\n", nonce);
					} else {
						log_error("could not satisfy request to re-send packet with id %lu\n", nonce);
					}
				}
				break;
			default:
				log_error("unknown segment of type %i found in client operation packet. Ignoring this piece\n", type);
				break;
		}
	}

	return true;
}

void intercom_handle_packet(intercom_ctx *ctx, uint8_t *packet, ssize_t packet_len, struct in6_addr *peer, uint16_t port) {
	intercom_packet_hdr *hdr = (intercom_packet_hdr *)packet;

	if (hdr->version == PACKET_FORMAT_VERSION) {
		intercom_recently_seen_add(ctx, hdr);
		if (hdr->type == CLIENT_OPERATION)
			intercom_handle_client_operation(ctx, (intercom_packet_op *)packet, packet_len, peer, port);
	} else {
		log_error(
		    "unknown packet with version %i received on intercom. Ignoring content and dropping the packet that could have "
		    "originated from: %s. This is a guess with current or previous positions of the originator\n",
		    hdr->version, print_ip((void *)&packet[6]));
	}
}


void intercom_init(intercom_ctx *ctx) {
	obtainrandom(&nonce, sizeof(uint32_t), 0);
	ctx->receivebuffer = NULL;

	VECTOR_INIT(ctx->recent_packets);
	VECTOR_INIT(ctx->missing_packets);

	ctx->fd = socket(PF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (ctx->fd < 0)
		exit_error("creating socket for intercom on node-IP");

	struct sockaddr_in6 server_addr = {
	    .sin6_family = AF_INET6, .sin6_port = htons(ctx->port),
	};

	if (bind(ctx->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind socket to node-IP failed");
		exit(EXIT_FAILURE);
	}
}

