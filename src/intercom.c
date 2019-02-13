#include "intercom.h"
#include "alloc.h"
#include "error.h"
#include "snapcast.h"
#include "syscallwrappers.h"
#include "timespec.h"
#include "util.h"
#include "alsaplayer.h"

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

#define NONCE_MAX 4294967294

uint32_t nonce = 0;

void schedule_request(struct intercom_task *data, int s_timeout, int ms_timeout, void (*processor)(void *data));
void schedule_hellos(struct intercom_task *data, int s_timeout, int ms_timeout, void (*processor)(void *data));

uint32_t get_nonce() { return htonl((nonce++ % NONCE_MAX)); }

void free_intercom_task(void *d) {
	struct intercom_task *data = d;
	free(data->packet);
	free(data->recipient);
	free(data);
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

int cmp_audiopacket(const audio_packet *ap1, const audio_packet *ap2) {
	if (ap1->nonce > ap2->nonce)
		return 1;
	else if (ap1->nonce < ap2->nonce)
		return -1;
	return 0;
}

int parse_op(uint8_t *packet, uint8_t *op) {
	packet[1] = 2;
	return 2;
}

int assemble_op(uint8_t *packet, uint32_t nonce, uint8_t op) {
	packet[0] = op;
	packet[1] = 2;
	return packet[1];
}

int assemble_request(uint8_t *packet, uint32_t nonce) {
	packet[0] = REQUEST;
	packet[1] = 6;
	uint32_t id = htonl(nonce);
	memcpy(&packet[2], &id, sizeof(uint32_t));
	return packet[1];
}


int assemble_hello(uint8_t *packet) {
	packet[0] = HELLO;
	packet[1] = 7;
	uint32_t n_nodeid = htonl(snapctx.intercom_ctx.nodeid);
	memcpy(&packet[2], &n_nodeid, sizeof(uint32_t));
	packet[6] = obtain_volume(&snapctx.alsaplayer_ctx);
	return packet[1];
}

bool intercom_send_packet_unicast(intercom_ctx *ctx, const struct in6_addr *recipient, uint8_t *packet, ssize_t packet_len, int port) {
	struct sockaddr_in6 addr = (struct sockaddr_in6){.sin6_family = AF_INET6, .sin6_port = htons(port), .sin6_addr = *recipient};

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

int parse_request(uint8_t *packet, uint32_t *nonce) {
	uint32_t tmp;
	memcpy(&tmp, &packet[2], sizeof(uint32_t));
	*nonce = ntohl(tmp);
	return packet[1];
}

int parse_hello(uint8_t *packet, client_t *client) {
	uint32_t nodeid;
	memcpy(&nodeid, &packet[2], sizeof(uint32_t));
	client->id = ntohl(nodeid);
	client->volume = packet[7];
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
			case REQUEST:
				currentoffset += parse_request(packetpointer, &nonce);
				audio_packet key = {.nonce = nonce};
				audio_packet *ap = VECTOR_LSEARCH(&key, snapctx.intercom_ctx.packet_buffer, cmp_audiopacket);
				if (ap) {
					intercom_send_packet_unicast(ctx, peer, ap->data, ap->len, port);
					log_error("RE-SENT PACKET WITH id %lu\n", nonce);
				} else {
					log_error("could not satisfy request to re-send packet with id %lu\n", nonce);
				}
				break;
			default:
				log_error("unknown segment of type %i found in client operation packet. Ignoring this piece\n", type);
				break;
		}
	}

	return true;
}

bool intercom_handle_server_operation(intercom_ctx *ctx, intercom_packet_sop *packet, int packet_len) {
	int currentoffset = sizeof(intercom_packet_hdr);
	uint8_t type, *packetpointer;

	while (currentoffset < packet_len) {
		packetpointer = &((uint8_t *)packet)[currentoffset];
		type = *packetpointer;
		log_debug("offset: %i %p %p\n", currentoffset, packet, packetpointer);
		switch (type) {
			case CLIENT_STOP:
				currentoffset += parse_op(packetpointer, NULL);
				pcmChunk p;
				while (ctx->bufferrindex < ctx->bufferwindex) {
					intercom_getnextaudiochunk(ctx, &p);
				}

				while (VECTOR_LEN(snapctx.intercom_ctx.missing_packets)) {
					VECTOR_DELETE(snapctx.intercom_ctx.missing_packets, 0);
				}
				memset(ctx->buffer, 0, sizeof(pcmChunk) * ctx->buffer_elements);
				snapctx.intercom_ctx.lastreceviedseqno = 0;
				// TODO: should we stop alsa here?
				break;
			default:
				log_error("unknown segment of type %i found in client operation packet. Ignoring this piece\n", type);
				currentoffset += packetpointer[2];
				break;
		}
	}
	return true;
}

struct timespec intercom_get_time_next_audiochunk(intercom_ctx *ctx) {
	struct timespec ret = {};
	if (ctx->bufferrindex < ctx->bufferwindex + ctx->buffer_wraparound * ctx->buffer_elements) {
		pcmChunk *buf = &ctx->buffer[ctx->bufferrindex];
		ret.tv_sec = buf->play_at_tv_sec;
		ret.tv_nsec = buf->play_at_tv_nsec;
	}
	return ret;
}

bool intercom_peeknextaudiochunk(intercom_ctx *ctx, pcmChunk **ret) {
	if (ctx->bufferrindex < ctx->bufferwindex + ctx->buffer_wraparound * ctx->buffer_elements) {
		*ret = &ctx->buffer[ctx->bufferrindex];
		return true;
	} else {
		return false;
	}
}

int ringbuffer_fill(intercom_ctx *ctx) { return ctx->bufferwindex - ctx->bufferrindex + ctx->buffer_wraparound * ctx->buffer_elements; }

void intercom_getnextaudiochunk(intercom_ctx *ctx, pcmChunk *ret) {
	pcmChunk *c;
	if (!intercom_peeknextaudiochunk(ctx, &c)) {
		log_error("BUFFER UNDERRUN\n");
		get_emptychunk(ret);
	} else {
		memcpy(ret, c, sizeof(pcmChunk));
	}

	log_verbose(
	    "retrieved audio chunk [size: %d, samples: %d, channels: %d, timestamp %zu.%zu] from readindex/writeindex: %zu/%zu, cached "
	    "chunks: %zu/%zu\n",
	    ret->size, ret->samples, ret->channels, ret->play_at_tv_sec, ret->play_at_tv_nsec, ctx->bufferrindex, ctx->bufferwindex,
	    ringbuffer_fill(ctx), ctx->buffer_elements);
	print_packet(ret->data, ret->size);

	if (ret->play_at_tv_sec > 0) {
		ctx->bufferrindex = (ctx->bufferrindex + 1) % ctx->buffer_elements;
		if (ctx->bufferrindex == 0)
			ctx->buffer_wraparound = 0;
	}
}

int ringbuffer_getprevindex(intercom_ctx *ctx, int i) {
	if (i == 0)
		return ctx->buffer_elements;

	return (i - 1);
}

int ringbuffer_getnextindex(intercom_ctx *ctx, int i) { return (i + 1) % ctx->buffer_elements; }

void bufferwindex_increment(intercom_ctx *ctx) {
	ctx->bufferwindex = (ctx->bufferwindex + 1) % ctx->buffer_elements;
	if (ctx->bufferwindex == 0)
		ctx->buffer_wraparound = 1;
}

void intercom_put_chunk_locate(intercom_ctx *ctx, pcmChunk *chunk) {
	int fill = ringbuffer_fill(ctx);

	struct timespec chunk_time = {.tv_sec = chunk->play_at_tv_sec, .tv_nsec = chunk->play_at_tv_nsec};

	while (fill) {
		int look_at = (ctx->bufferrindex + fill - 1) % ctx->buffer_elements;

		struct timespec elem_time = {.tv_sec = ctx->buffer[look_at].play_at_tv_sec, .tv_nsec = ctx->buffer[look_at].play_at_tv_nsec};

		if (timespec_cmp(elem_time, chunk_time) < 0) {
			log_debug("placing late chunk in buffer at position %d: %s chunk to be sorted: %s\n", look_at,
				  print_chunk(&ctx->buffer[look_at]), print_chunk(chunk));
			memcpy(&ctx->buffer[(look_at + 1) % ctx->buffer_elements], chunk, sizeof(pcmChunk));
			bufferwindex_increment(ctx);
			break;
		} else {  // copy one element up
			log_debug("copying up - chunk from buffer: %s chunk to be sorted: %s\n", print_chunk(&ctx->buffer[look_at]),
				  print_chunk(chunk));
			memcpy(&ctx->buffer[(look_at + 1) % ctx->buffer_elements], &ctx->buffer[look_at], sizeof(pcmChunk));
		}

		--fill;
	}
}

void intercom_put_chunk(intercom_ctx *ctx, pcmChunk *chunk) {
	log_debug("wrote chunk to buffer readindex: %d elements: %d to writeindex: %d\n", ctx->bufferrindex, ctx->buffer_elements, ctx->bufferwindex);
	//	print_packet(chunk->data, chunk->size);

	// TODO protect from over-writing unread data would be nice in the client in addition to the server - We are leaking memory if overwriting
	// happens.
	memcpy(&ctx->buffer[ctx->bufferwindex], chunk, sizeof(pcmChunk));
	bufferwindex_increment(ctx);
}

bool remove_request(uint32_t nonce) {
	audio_packet key = {.nonce = nonce};
	audio_packet *ap = VECTOR_LSEARCH(&key, snapctx.intercom_ctx.missing_packets, cmp_audiopacket);

	if (ap) {
		int i = VECTOR_GETINDEX(snapctx.intercom_ctx.missing_packets, ap);
		VECTOR_DELETE(snapctx.intercom_ctx.missing_packets, i);
		return true;
	}
	return false;
}

uint32_t get_nonce_from_packet(uint8_t *packet) {
	uint32_t tmp;
	intercom_packet_hdr *hdr = (intercom_packet_hdr *)(packet);
	memcpy(&tmp, &(hdr->nonce), sizeof(uint32_t));

	return ntohl(tmp);
}

uint32_t get_nonce_from_tlv(uint8_t *tlv) {
	uint32_t tmp;
	memcpy(&tmp, &tlv[2], sizeof(uint32_t));

	return ntohl(tmp);
}

void request_task(void *d) {
	struct intercom_task *data = d;
	struct intercom_task *ndata = snap_alloc0(sizeof(struct intercom_task));
	intercom_packet_hdr *req = (intercom_packet_hdr *)data->packet;
	req->nonce = get_nonce();

	uint32_t req_nonce = get_nonce_from_tlv(&data->packet[sizeof(intercom_packet_hdr)]);

	if (data->retries_left > 0) {
		audio_packet key = {.nonce = req_nonce};
		audio_packet *ap = VECTOR_LSEARCH(&key, snapctx.intercom_ctx.missing_packets, cmp_audiopacket);
		if (ap) {
			log_debug("Requesting missing packet with id %lu\n", req_nonce);
			copy_intercom_task(data, ndata);
			ndata->retries_left--;

			intercom_send_packet_unicast(&snapctx.intercom_ctx, data->recipient, (uint8_t *)data->packet, data->packet_len,
						     snapctx.intercom_ctx.serverport);
			schedule_request(ndata, 0, 100, request_task);
		} else {
			log_debug("Could not find request for id %lu - it was most likely already served.\n", req_nonce);
		}
	} else {
		log_error("no more retries left for packet with id %lu - we are missing data due to shaky network. This will be audible.\n",
			  req_nonce);
		remove_request(req_nonce);
	}
}

void intercom_send_request(intercom_ctx *ctx, audio_packet *mp) {
	struct intercom_task *data = snap_alloc0(sizeof(struct intercom_task));
	data->packet = snap_alloc(sizeof(intercom_packet_op) + sizeof(tlv_request));

	data->packet_len = assemble_header(&((intercom_packet_op *)data->packet)->hdr, CLIENT_OPERATION);
	data->packet_len += assemble_request(&data->packet[data->packet_len], mp->nonce);
	log_debug("assembling request packet for nonce: %lu\n", mp->nonce);

	int interval_ms = 100;

	data->retries_left = snapctx.bufferms / interval_ms;
	data->check_task = NULL;

	data->recipient = snap_alloc_aligned(sizeof(struct in6_addr), 16);
	memcpy(data->recipient, &ctx->serverip, sizeof(struct in6_addr));

	data->check_task = post_task(&snapctx.taskqueue_ctx, 0, 0, request_task, free_intercom_task, data);
}

bool is_next_chunk(uint32_t seq) {
	intercom_ctx *ctx = &snapctx.intercom_ctx;

	return (((seq == 0) && (ctx->lastreceviedseqno == NONCE_MAX)) ||
		((seq - 1) == ctx->lastreceviedseqno && ctx->lastreceviedseqno != NONCE_MAX));
};

bool intercom_handle_audio(intercom_ctx *ctx, intercom_packet_audio *packet, int packet_len) {
	// TODO: Implementing TLV format for audio data will de-couple readms from the packet size.
	realloc_intercom_buffer_when_required(ctx, ntohs(packet->bufferms));

	uint8_t *packetpointer = &((uint8_t *)packet)[sizeof(intercom_packet_audio)];

	pcmChunk chunk;
	pcmChunk *pchunk = (pcmChunk *)packetpointer;

	chunk_copy_meta(&chunk, pchunk);
	chunk_ntoh(&chunk);
	chunk.data = snap_alloc(chunk.size);

	int currentoffset = CHUNK_HEADER_SIZE + sizeof(intercom_packet_audio);
	memcpy(chunk.data, &((uint8_t *)packet)[currentoffset], chunk.size);

	size_t this_seqno = ntohl(packet->hdr.nonce);

	log_debug("read chunk from packet: %d samples: %d frame size:%d  channels: %d packet_len: %d, hdrsize: %d\n\n", chunk.size, chunk.samples,
		  chunk.frame_size, chunk.channels, packet_len, sizeof(intercom_packet_audio));

	//	print_packet((void*)pchunk, 17);

	if (!is_next_chunk(this_seqno) && this_seqno - ctx->lastreceviedseqno < 1000) {
		struct timespec ctime;
		obtainsystime(&ctime);
		log_error("Compensating packet loss for %lu packets: Last received audio chunk had seqno %lu, we just received %lu.\n",
			  this_seqno - ctx->lastreceviedseqno - 1, ctx->lastreceviedseqno, this_seqno);

		// TODO: place multiple TLV for request into a single packet for more efficiency
		for (uint32_t i = this_seqno - 1; i > ctx->lastreceviedseqno; --i) {
			log_verbose("requested packet with seqno: %lu\n", i);
			audio_packet ap = {.nonce = i};
			VECTOR_ADD(snapctx.intercom_ctx.missing_packets, ap);
			intercom_send_request(ctx, &ap);
		}
	}

	if (this_seqno > ctx->lastreceviedseqno) {
		intercom_put_chunk(ctx, &chunk);
		ctx->lastreceviedseqno = this_seqno;
	} else {
		if (remove_request(this_seqno)) {
			log_error("Processing out of order audio with seqno: %lu\n", this_seqno);
			intercom_put_chunk_locate(ctx, &chunk);
		}
	}

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

void remove_old_audiodata_task(void *data) {
	//	struct buffer_cleanup_task *ct = (struct buffer_cleanup_task*) data;
	/// audio_packet *ap = (audio_packet*)VECTOR_LSEARCH(&ct->ap, snapctx.intercom_ctx.packet_buffer ,cmp_audiopacket);
	audio_packet ap = VECTOR_INDEX(snapctx.intercom_ctx.packet_buffer, 0);
	free(ap.data);

	VECTOR_DELETE(snapctx.intercom_ctx.packet_buffer, 0);  // always remove the oldest element
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

	// print_packet(packet, packet_len);

	audio_packet ap;
	ap.data = snap_alloc(packet_len);
	memcpy(ap.data, packet, packet_len);
	ap.len = packet_len;
	ap.nonce = ntohl(((intercom_packet_audio *)packet)->hdr.nonce);
	VECTOR_ADD(ctx->packet_buffer, ap);

	// since we always write into this vector in ascending order, we can always remove the very first item on timeout and thus do not have to pass
	// the information which packet to remove
	post_task(&snapctx.taskqueue_ctx, (snapctx.bufferms / 1000) + 1, snapctx.bufferms % 1000, remove_old_audiodata_task, NULL, NULL);

	for (int i = VECTOR_LEN(snapctx.clientmgr_ctx.clients) - 1; i >= 0; i--) {
		struct client *c = &VECTOR_INDEX(snapctx.clientmgr_ctx.clients, i);
		intercom_send_packet_unicast(&snapctx.intercom_ctx, &c->ip, packet, packet_len, c->port);
	}
}

void schedule_request(struct intercom_task *data, const int s_timeout, const int ms_timeout, void (*processor)(void *data)) {
	data->check_task = post_task(&snapctx.taskqueue_ctx, s_timeout, ms_timeout, processor, NULL, data);
}

void hello_task(void *d) {
	log_verbose("saying hello to server\n");
	struct intercom_task *data = d;
	intercom_packet_hello *hello = (intercom_packet_hello *)data->packet;

	hello->hdr.nonce = get_nonce();

	int packet_len = sizeof(hello->hdr);

	packet_len += assemble_hello((void *)(&data->packet[packet_len]));

	intercom_send_packet_unicast(&snapctx.intercom_ctx, data->recipient, (uint8_t *)data->packet, data->packet_len,
				     snapctx.intercom_ctx.serverport);
	schedule_hellos(data, 1, 500, hello_task);
}

void schedule_hellos(struct intercom_task *data, const int s_timeout, const int ms_timeout, void (*processor)(void *data)) {
	data->check_task = post_task(&snapctx.taskqueue_ctx, s_timeout, ms_timeout, processor, NULL, data);
}

bool intercom_stop_client(intercom_ctx *ctx, const struct in6_addr *recipient, int port) {
	int packet_len = 0;
	uint8_t *packet = snap_alloc(sizeof(intercom_packet_op) + sizeof(tlv_op));

	packet_len = assemble_header(&((intercom_packet_op *)packet)->hdr, SERVER_OPERATION);
	packet_len += assemble_op(&packet[packet_len], get_nonce(), CLIENT_STOP);

	intercom_send_packet_unicast(&snapctx.intercom_ctx, recipient, packet, packet_len, snapctx.intercom_ctx.serverport);
}

bool intercom_hello(intercom_ctx *ctx, const struct in6_addr *recipient, int port) {
	struct intercom_task *data = snap_alloc0(sizeof(struct intercom_task));

	data->packet_len = sizeof(intercom_packet_op) + sizeof(tlv_hello);
	data->packet = snap_alloc(data->packet_len);

	assemble_header(&((intercom_packet_op *)data->packet)->hdr, CLIENT_OPERATION);

	if (recipient) {
		data->recipient = snap_alloc_aligned(sizeof(struct in6_addr), 16);
		memcpy(data->recipient, recipient, sizeof(struct in6_addr));
	}
	data->check_task = post_task(&snapctx.taskqueue_ctx, 0, 0, hello_task, NULL, data);
	return true;
}

