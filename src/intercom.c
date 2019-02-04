#include "intercom.h"
#include "alloc.h"
#include "error.h"
#include "snapcast.h"
#include "syscallwrappers.h"
#include "timespec.h"
#include "util.h"

#include "clientmgr.h"
#include "pcmchunk.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define INTERCOM_MAX_RECENT 1000

uint32_t nonce = 0;

void schedule_hellos(struct intercom_task *data, int s_timeout, int ms_timeout, void (*processor)(void *data));

uint32_t get_nonce() { return htonl((nonce++ % 4294967294)); }

void free_intercom_task(void *d) {
	struct intercom_task *data = d;
	free(data->packet);
	free(data->recipient);
	free(data);
}

void realloc_intercom_buffer_when_required(intercom_ctx *ctx, int serverbufferms) {
	if (serverbufferms / snapctx.readms != ctx->buffer_elements) {
		log_error("Adjusting local buffer size to %d ms\n", serverbufferms);
		free(ctx->buffer);
		ctx->bufferrindex = ctx->bufferwindex = 0;
		ctx->buffer_elements = serverbufferms / snapctx.readms;
		ctx->buffer = snap_alloc(sizeof(pcmChunk) * ctx->buffer_elements);
		memset(ctx->buffer, 0, sizeof(pcmChunk) * ctx->buffer_elements);
		snapctx.bufferms = serverbufferms;
	}
}

void intercom_init(intercom_ctx *ctx) {
	obtainrandom(&nonce, sizeof(uint32_t), 0);
	ctx->bufferwindex = 0;
	ctx->bufferrindex = 0;
	ctx->buffer = 0;

	struct sockaddr_in6 server_addr = {
	    .sin6_family = AF_INET6, .sin6_port = htons(ctx->port),
	};

	ctx->fd = socket(PF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (ctx->fd < 0)
		exit_error("creating socket for intercom on node-IP");

	// TODO: clients should connect() server should bind. How do we know when called as server?
	if (bind(ctx->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind socket to node-IP failed");
		exit(EXIT_FAILURE);
	}

	realloc_intercom_buffer_when_required(ctx, snapctx.bufferms);
}

int assemble_header(intercom_packet_hdr *hdr, uint8_t type) {
	hdr->version = PACKET_FORMAT_VERSION;
	hdr->type = type;
	hdr->empty = 0;
	hdr->nonce = get_nonce();
	return sizeof(intercom_packet_hdr);
}

int assemble_hello(uint8_t *packet) {
	packet[0] = HELLO;
	packet[1] = 6;
	uint32_t n_nodeid = htonl(snapctx.intercom_ctx.nodeid);
	memcpy(&packet[2], &n_nodeid, sizeof(uint32_t));
	return packet[1];
}

bool intercom_send_packet_unicast(intercom_ctx *ctx, const struct in6_addr *recipient, uint8_t *packet, ssize_t packet_len, int port) {
	intercom_packet_hdr *hdr = (intercom_packet_hdr *)packet;
	if (hdr->type == AUDIO_DATA) {
		// TODO: packet into buffer to allow answering REQUESTs
		log_debug("we should put this audio packet into the buffer - REQUEST not implemented yet\n");
	}

	struct sockaddr_in6 addr = (struct sockaddr_in6){.sin6_family = AF_INET6, .sin6_port = htons(port), .sin6_addr = *recipient};

	log_debug("fd: %i, packet %p, length: %zi %s\n", ctx->fd, packet, packet_len, print_ip(recipient));
	int rc = sendto(ctx->fd, packet, packet_len, 0, (struct sockaddr *)&addr, sizeof(addr));
	log_debug("sent intercom packet rc: %zi to %s\n", rc, print_ip(recipient));

	if (rc < 0)
		perror("sendto failed");

	return rc >= 0;
}

bool intercom_recently_seen(intercom_ctx *ctx, intercom_packet_hdr *hdr) {
	for (int i = 0; i < VECTOR_LEN(ctx->recent_packets); i++) {
		intercom_packet_hdr *ref_hdr = &VECTOR_INDEX(ctx->recent_packets, i);

		if (ref_hdr->nonce == hdr->nonce && ref_hdr->type == hdr->type)
			return true;
	}
	return false;
}

void intercom_recently_seen_add(intercom_ctx *ctx, intercom_packet_hdr *hdr) {
	while (VECTOR_LEN(ctx->recent_packets) > INTERCOM_MAX_RECENT) VECTOR_DELETE(ctx->recent_packets, 0);

	VECTOR_ADD(ctx->recent_packets, *hdr);
}

int parse_address(const uint8_t *packet, struct in6_addr *address) {
	log_debug("parsing seek packet segment: address\n");
	memcpy(address, &packet[4], 16);
	return packet[1];
}

int parse_request(uint8_t *packet, client_t *client) {
	// TODO: implement me: extract list of seqno that reference packets we sent, and re-send them.
	return packet[1];
}

int parse_hello(uint8_t *packet, client_t *client) {
	uint32_t nodeid;
	memcpy(&nodeid, &packet[2], sizeof(uint32_t));
	client->id = ntohl(nodeid);
	return packet[1];
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

	while (currentoffset < packet_len) {
		packetpointer = &((uint8_t *)packet)[currentoffset];
		type = *packetpointer;
		log_debug("offset: %i %p %p\n", currentoffset, packet, packetpointer);
		switch (type) {
			case HELLO:
				currentoffset += parse_hello(packetpointer, &client);
				// TODO: send stream information (samples, channels, frame_size);
				break;
			case REQUEST:
				currentoffset += parse_request(packetpointer, &client);
				break;
			default:
				log_error("unknown segment of type %i found in info packet. ignoring this piece\n", type);
				break;
		}
	}

	memcpy(&client.ip, peer, sizeof(struct in6_addr));
	client.port = port;
	clientmgr_refresh_client(&client);

	return true;
}

bool intercom_handle_server_operation(intercom_ctx *ctx, intercom_packet_sop *packet, int packet_len) {
	/*
	** Parameter einstellen für sample rate, channels und sample size
	*/
	return true;
}

struct timespec intercom_get_time_next_audiochunk(intercom_ctx *ctx) {
	struct timespec ret = {};
	if (ctx->bufferrindex < ctx->bufferwindex + ctx->buffer_wraparound * ctx->buffer_elements) {
		pcmChunk *buf;
		buf = (void *)&((char *)ctx->buffer)[sizeof(pcmChunk) * ctx->bufferrindex];
		ret.tv_sec = buf->play_at_tv_sec;
		ret.tv_nsec = buf->play_at_tv_nsec;
	}
	return ret;
}

void intercom_peeknextaudiochunk(intercom_ctx *ctx, pcmChunk *ret) {
	if (ctx->bufferrindex < ctx->bufferwindex + ctx->buffer_wraparound * ctx->buffer_elements) {
		pcmChunk *buf = (pcmChunk *)&((char *)ctx->buffer)[sizeof(pcmChunk) * ctx->bufferrindex];
		memcpy(ret, buf, sizeof(pcmChunk));
	} else {
		log_error("BUFFER UNDERRUN\n");
		get_emptychunk(ret);
	}
}

void intercom_getnextaudiochunk(intercom_ctx *ctx, pcmChunk *ret) {
	intercom_peeknextaudiochunk(ctx, ret);

	log_verbose(
	    "retrieved audio chunk [size: %d, samples: %d, channels: %d, timestamp %zu.%zu] from readindex/writeindex: %zu/%zu, cached "
	    "chunks: %zu/%zu\n",
	    ret->size, ret->samples, ret->channels, ret->play_at_tv_sec, ret->play_at_tv_nsec, ctx->bufferrindex, ctx->bufferwindex,
	    ctx->bufferwindex - ctx->bufferrindex + ctx->buffer_wraparound * ctx->buffer_elements, ctx->buffer_elements);
	print_packet(ret->data, ret->size);

	if (ret->play_at_tv_sec > 0) {
		ctx->bufferrindex = (ctx->bufferrindex + 1) % ctx->buffer_elements;
		if (ctx->bufferrindex == 0)
			ctx->buffer_wraparound = 0;
	}
}

void intercom_put_chunk(intercom_ctx *ctx, pcmChunk *chunk) {
	log_debug("wrote chunk to buffer readindex: %d elements: %d to writeindex: %d\n", ctx->bufferrindex, ctx->buffer_elements, ctx->bufferwindex);
	print_packet(chunk->data, chunk->size);

	// todo: protect from over-writing unread data - otherwise we are leaking memory.:w
	memcpy(&((uint8_t *)ctx->buffer)[sizeof(pcmChunk) * ctx->bufferwindex], chunk, sizeof(pcmChunk));
	ctx->bufferwindex = (ctx->bufferwindex + 1) % ctx->buffer_elements;
	if (ctx->bufferwindex == 0)
		ctx->buffer_wraparound = 1;
}

int min(int a, int b) {
	if (a > b)
		return b;
	return a;
}

bool intercom_handle_audio(intercom_ctx *ctx, intercom_packet_audio *packet, int packet_len) {
	// TODO: place packet in cache
	//  start decoder for Paket
	// TODO: change packet format, implement TLV for audio data.

	realloc_intercom_buffer_when_required(ctx, ntohs(packet->bufferms));

	uint8_t *packetpointer = &((uint8_t *)packet)[sizeof(intercom_packet_audio)];

	pcmChunk chunk;
	pcmChunk *pchunk = (pcmChunk *)packetpointer;

	chunk_copy_meta(&chunk, pchunk);
	chunk_ntoh(&chunk);
	chunk.data = snap_alloc(chunk.size);

	int currentoffset = CHUNK_HEADER_SIZE + sizeof(intercom_packet_audio);

	memcpy(chunk.data, &((uint8_t*)packet)[currentoffset], chunk.size);

	if (chunk.codec == CODEC_OPUS) {
		log_debug("Decoding opus data\n");
		decode_opus_handle(&chunk);
	}

	log_debug("read chunk from packet: %d samples: %d frame size:%d  channels: %d packet_len: %d, hdrsize: %d\n\n", chunk.size, chunk.samples,
		  chunk.frame_size, chunk.channels, packet_len, sizeof(intercom_packet_audio));

	intercom_put_chunk(ctx, &chunk);


	// initialize audio and decoder if not already done
	// TODO: THIS DOES NOT BELONG HERE! FIND A WAY TO DO THIS IN CLIENT.C
	if (! snapctx.opuscodec_ctx.decoder) {
		snapctx.samples = chunk.samples;
		snapctx.channels = chunk.channels;
		snapctx.frame_size = chunk.frame_size;
		opus_init_decoder();
	}


	ctx->lastreceviedseqno = ntohl(packet->hdr.nonce);

	return true;
}

void intercom_handle_packet(intercom_ctx *ctx, uint8_t *packet, ssize_t packet_len, struct in6_addr *peer, uint16_t port) {
	intercom_packet_hdr *hdr = (intercom_packet_hdr *)packet;

	if (hdr->version == PACKET_FORMAT_VERSION) {
		if (intercom_recently_seen(ctx, hdr))
			return;

		intercom_recently_seen_add(ctx, hdr);
		if (hdr->type == CLIENT_OPERATION)
			intercom_handle_client_operation(ctx, (intercom_packet_op *)packet, packet_len, peer, port);

		if (hdr->type == SERVER_OPERATION)
			intercom_handle_server_operation(ctx, (intercom_packet_sop *)packet, packet_len);

		if (hdr->type == AUDIO_DATA)
			intercom_handle_audio(ctx, (intercom_packet_audio *)packet, packet_len);

	} else {
		log_error(
		    "unknown packet with version %i received on intercom. Ignoring content and dropping the packet that could have originated from: "
		    "%s. This is a guess with current or previous positions of the originator\n",
		    hdr->version, print_ip((void *)&packet[6]));
	}
}

void intercom_handle_in(intercom_ctx *ctx, int fd) {
	ssize_t count;
	uint8_t buf[ctx->mtu];

	log_debug("HANDLING INTERCOM PACKET on fd %i using buffersize of %i ", fd, ctx->mtu);

	while (1) {
		struct sockaddr_in6 peer_addr;

		unsigned int peer_addr_len = sizeof(struct sockaddr_storage);
		count = recvfrom(fd, buf, ctx->mtu, 0, (struct sockaddr *)&peer_addr, &peer_addr_len);

		if (count == -1) {
			/* If errno == EAGAIN, that means we have read all data. So go back to the main loop. if the last intercom packet was a claim
			 * for a local client, then we have just dropped the local client and will receive EBADF on the fd for the node-client-IP.
			 * This is not an error.*/
			if (errno == EBADF) {
				perror("there is something crazy going on. - returning to the main loop");
				log_error("the EBADF happened on fd: %i\n", fd);
			} else if (errno != EAGAIN) {
				perror("read error - this should not happen - going back to main loop");
			}
			break;
		} else if (count == 0) {
			/* End of file. The remote has closed the connection. */
			break;
		}

		struct in6_addr peer;
		memcpy(&peer.s6_addr, &peer_addr.sin6_addr, sizeof(struct in6_addr));

		uint16_t port = ntohs(peer_addr.sin6_port);
		log_debug("- read from %s(%d), %zi Bytes of data: ", print_ip(&peer), port, count);
		print_packet(buf, count);

		intercom_handle_packet(ctx, buf, count, &peer, port);
	}
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
/** send chunk to all clients that are currently active
*/
void intercom_send_audio(intercom_ctx *ctx, pcmChunk *chunk) {
	int chunksize = CHUNK_HEADER_SIZE + chunk->size;
 	log_debug("sending %d Bytes of audio data\n", chunk->size);
	uint8_t packet[sizeof(intercom_packet_audio) + chunksize];

	ssize_t packet_len;
	packet_len = assemble_header(&((intercom_packet_audio *)packet)->hdr, AUDIO_DATA);

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

	print_packet(packet, packet_len);
	for (int i = VECTOR_LEN(snapctx.clientmgr_ctx.clients) - 1; i >= 0; i--) {
		struct client *c = &VECTOR_INDEX(snapctx.clientmgr_ctx.clients, i);
		intercom_send_packet_unicast(&snapctx.intercom_ctx, &c->ip, packet, packet_len, c->port);
	}
}

void hello_task(void *d) {
	log_verbose("saying hello to server\n");
	struct intercom_task *data = d;
	intercom_packet_hello *hello = (intercom_packet_hello *)data->packet;
	hello->hdr.nonce = get_nonce();

	intercom_send_packet_unicast(&snapctx.intercom_ctx, data->recipient, (uint8_t *)data->packet, data->packet_len,
				     snapctx.intercom_ctx.serverport);
	schedule_hellos(data, 1, 500, hello_task);
}

void copy_intercom_task(struct intercom_task *old, struct intercom_task *new) {
	new->packet_len = old->packet_len;
	new->packet = snap_alloc(old->packet_len);
	memcpy(new->packet, old->packet, new->packet_len);

	new->recipient = NULL;
	new->check_task = old->check_task;
	if (old->recipient) {
		new->recipient = snap_alloc_aligned(sizeof(struct in6_addr), sizeof(struct in6_addr));
		memcpy(new->recipient, old->recipient, sizeof(struct in6_addr));
	}

	new->retries_left = old->retries_left;
}

void schedule_hellos(struct intercom_task *data, const int s_timeout, const int ms_timeout, void (*processor)(void *data)) {
	data->check_task = post_task(&snapctx.taskqueue_ctx, s_timeout, ms_timeout, processor, NULL, data);
}

bool intercom_hello(intercom_ctx *ctx, const struct in6_addr *recipient, int port) {
	struct intercom_task *data = snap_alloc(sizeof(struct intercom_task));
	data->packet = snap_alloc(sizeof(intercom_packet_op) + sizeof(tlv_hello));

	data->packet_len = assemble_header(&((intercom_packet_op *)data->packet)->hdr, CLIENT_OPERATION);
	data->packet_len += assemble_hello((void *)(&data->packet[data->packet_len]));

	data->retries_left = 12;  // hellos will never stop repeating because the task will increment this value. Just set it to something above 0
	data->check_task = NULL;

	if (recipient) {
		data->recipient = snap_alloc_aligned(sizeof(struct in6_addr), 16);
		memcpy(data->recipient, recipient, sizeof(struct in6_addr));
	}
	data->check_task = post_task(&snapctx.taskqueue_ctx, 0, 0, hello_task, NULL, data);
	return true;
}
