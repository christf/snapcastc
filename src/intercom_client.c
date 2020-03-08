#include "snapcast.h"
#include "alsaplayer.h"
#include "intercom_common.h"
#include "intercom_client.h"
#include "util.h"
#include "alloc.h"
#include "timespec.h"
#include "syscallwrappers.h"

extern uint32_t nonce;
#define REQUEST_RETRY_INTERVAL_MS 100

int receivebuffer_cmp(const void *d1, const void *d2) {
	pcmChunk *c1 = (pcmChunk *)d1;
	pcmChunk *c2 = (pcmChunk *)d2;

	return chunk_cmp(c2, c1);  // we want smallest first
}

void realloc_intercom_buffer(intercom_ctx *ctx, size_t elements) {
	log_verbose("Adjusting an initializing local buffer size to %d elements. Previous size: %d elements.\n", elements,
		    ctx->receivebuffer ? ctx->receivebuffer->capacity : 0);
	pqueue_delete(ctx->receivebuffer);
	ctx->receivebuffer = pqueue_new(receivebuffer_cmp, elements);
}

void realloc_intercom_buffer_when_required(intercom_ctx *ctx, int serverbufferms, size_t chunk_ms) {
	// should we be able to do this when retaining the content of the buffer?
	size_t calculated_elements = serverbufferms / chunk_ms + 1;
	if (!ctx->receivebuffer || calculated_elements != ctx->receivebuffer->capacity) {
		realloc_intercom_buffer(ctx, calculated_elements);
		snapctx.bufferms = serverbufferms;
	}
}

int parse_volume(uint8_t *packet, uint8_t *volume) {
	*volume = packet[2];
	return packet[1];
}

void limit_missing_packets(intercom_ctx *ctx, int maxsize) {
	while (VECTOR_LEN(ctx->missing_packets) > maxsize) {
		log_error("We have more missing packets (%d) registered than we can hold in our packet buffer (%d). Dropping oldest.",
			  VECTOR_LEN(ctx->missing_packets), maxsize);

		audio_packet *ap = &VECTOR_INDEX(ctx->missing_packets, 0);
		free(ap->data);
		ap->data = NULL;
		VECTOR_DELETE(ctx->missing_packets, 0);
	}
}

void prune_missing_packets(intercom_ctx *ctx, uint32_t oldestnonce) {
	for (int i = VECTOR_LEN(ctx->missing_packets) - 1; i >= 0; --i) {
		audio_packet *ap = &VECTOR_INDEX(ctx->missing_packets, i);

		if (ap->nonce <= oldestnonce) {
			VECTOR_DELETE(ctx->missing_packets, i);
			log_error("deleting outdated packet from missing packets vector\n");
		}
	}
}


int assemble_hello(uint8_t *packet) {
	packet[0] = HELLO;
	packet[1] = 11;
	uint32_t n_tmp = htonl(snapctx.intercom_ctx.nodeid);
	memcpy(&packet[2], &n_tmp, sizeof(uint32_t));
	n_tmp = htonl(snapctx.alsaplayer_ctx.latency_ms);
	memcpy(&packet[6], &n_tmp, sizeof(uint32_t));
	packet[10] = obtain_volume(&snapctx.alsaplayer_ctx);
	return packet[1];
}

void hello_task(void *d) {
	log_verbose("saying hello to server\n");
	struct intercom_task *data = d;
	intercom_packet_hello *hello = (intercom_packet_hello *)data->packet;

	hello->hdr.nonce = get_nonce(&nonce);

	int packet_len = sizeof(hello->hdr);

	packet_len += assemble_hello((void *)(&data->packet[packet_len]));

	intercom_send_packet_unicast(&snapctx.intercom_ctx, data->recipient, (uint8_t *)data->packet, data->packet_len, snapctx.intercom_ctx.port);
	data->ctx->hello_task = post_task(&snapctx.taskqueue_ctx, 1, 500, hello_task, NULL, data);
}


struct timespec intercom_get_time_next_audiochunk(intercom_ctx *ctx) {
	struct timespec ret = {};
	pcmChunk *buf = pqueue_peek(ctx->receivebuffer);
	return chunk_get_play_at(buf);
}

static bool underrun = false;

bool intercom_getnextaudiochunk(intercom_ctx *ctx, pcmChunk *ret) {
	pcmChunk *c = pqueue_dequeue(ctx->receivebuffer);
	if (!c) {
		bool is_consecutive_underrun = underrun;
		if (is_consecutive_underrun)
			log_verbose("NETWORK BUFFER UNDERRUN\n");
		else
			log_error("NETWORK BUFFER UNDERRUN\n");

		underrun = true;
	} else {
		underrun = false;
		log_verbose("retrieved audio chunk [size: %d, samples: %d, channels: %d, timestamp %zu.%zu]  cached chunks: %zu/%zu\n", c->size,
			    c->samples, c->channels, c->play_at_tv_sec, c->play_at_tv_nsec, ctx->receivebuffer->size, ctx->receivebuffer->capacity);
		print_packet(c->data, c->size);

		if (ret) {
			memcpy(ret, c, sizeof(pcmChunk));
		}
		free(c);
	}
	return !!c;
}

bool intercom_peeknextaudiochunk(intercom_ctx *ctx, pcmChunk **ret) {
	*ret = pqueue_peek(ctx->receivebuffer);
	return (!!ret);
}

bool intercom_hello(intercom_ctx *ctx, const struct in6_addr *recipient, const int port) {
	struct intercom_task *data = snap_alloc0(sizeof(struct intercom_task));

	data->packet_len = sizeof(intercom_packet_op) + sizeof(tlv_hello);
	data->packet = snap_alloc(data->packet_len);

	log_verbose("intercom hello nonce: %lu\n", nonce);
	assemble_header(&((intercom_packet_op *)data->packet)->hdr, CLIENT_OPERATION, &nonce, ctx->nodeid);

	if (recipient) {
		data->recipient = snap_alloc_aligned(sizeof(struct in6_addr), 16);
		memcpy(data->recipient, recipient, sizeof(struct in6_addr));
	}
	data->ctx = ctx;
	ctx->hello_task = post_task(&snapctx.taskqueue_ctx, 0, 0, hello_task, NULL, data);
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
				currentoffset += tlv_get_length(packetpointer);

				while (VECTOR_LEN(snapctx.intercom_ctx.missing_packets)) {
					VECTOR_DELETE(snapctx.intercom_ctx.missing_packets, 0);
				}
				pqueue_delete(ctx->receivebuffer);
				ctx->receivebuffer = NULL;
				snapctx.intercom_ctx.lastreceviedseqno = 0;
				// TODO: should we stop alsa here?
				break;
			case CLIENT_VOLUME:;
				uint8_t volume;
				currentoffset += parse_volume(packetpointer, &volume);
				log_error("handling volume change to %d %%\n", volume);
				adjustVolume(&snapctx.alsaplayer_ctx, volume);
				break;
			default:
				log_error("unknown segment of type %i found in client operation packet. Ignoring this piece\n", type);
				currentoffset += packetpointer[2];
				break;
		}
	}
	return true;
}

uint32_t get_nonce_from_tlv(uint8_t *tlv) {
	uint32_t tmp;
	memcpy(&tmp, &tlv[2], sizeof(uint32_t));

	return ntohl(tmp);
}

void copy_intercom_task(struct intercom_task *old, struct intercom_task *new) {
	new->packet_len = old->packet_len;
	new->packet = snap_alloc(old->packet_len);
	memcpy(new->packet, old->packet, new->packet_len);

	new->recipient = NULL;
	new->ctx = old->ctx;
	if (old->recipient) {
		new->recipient = snap_alloc_aligned(sizeof(struct in6_addr), sizeof(struct in6_addr));
		memcpy(new->recipient, old->recipient, sizeof(struct in6_addr));
	}

	new->retries_left = old->retries_left;
}

bool remove_request(uint32_t nonce) {
	audio_packet key = {.nonce = nonce};
	audio_packet *ap = VECTOR_LSEARCH(&key, snapctx.intercom_ctx.missing_packets, cmp_audiopacket);

	if (ap) {
		int i = VECTOR_GETINDEX(snapctx.intercom_ctx.missing_packets, ap);
		VECTOR_DELETE(snapctx.intercom_ctx.missing_packets, i);
		log_verbose("removing request %lu from missing_packets vector index %d\n", nonce, i);
		return true;
	}
	return false;
}

int tlv_get_length(uint8_t *packet) { return packet[1]; }

void free_intercom_task(void *d) {
	struct intercom_task *data = d;
	free(data->packet);
	free(data->recipient);
	free(data);
}

void request_task(void *d) {
	struct intercom_task *data = d;
	intercom_packet_hdr *req = (intercom_packet_hdr *)data->packet;
	req->nonce = get_nonce(&nonce);

	uint32_t req_nonce = get_nonce_from_tlv(&data->packet[sizeof(intercom_packet_hdr)]);

	if (data->retries_left > 0) {
		audio_packet key = {.nonce = req_nonce};
		audio_packet *ap = VECTOR_LSEARCH(&key, snapctx.intercom_ctx.missing_packets, cmp_audiopacket);
		if (ap) {
			struct intercom_task *ndata = snap_alloc0(sizeof(struct intercom_task));
			log_debug("Requesting missing packet with id %lu\n", req_nonce);
			copy_intercom_task(data, ndata);
			ndata->retries_left--;

			intercom_send_packet_unicast(&snapctx.intercom_ctx, data->recipient, (uint8_t *)data->packet, data->packet_len,
						     snapctx.intercom_ctx.port);
			post_task(&snapctx.taskqueue_ctx, 0, REQUEST_RETRY_INTERVAL_MS, request_task, free_intercom_task, ndata);
		} else {
			log_debug("Could not find request for id %lu - it was most likely already served.\n", req_nonce);
		}
	} else {
		log_error("no more retries left for packet with id %lu - we are missing data due to shaky network. This will be audible.\n",
			  req_nonce);
		remove_request(req_nonce);
	}
}

int assemble_request(uint8_t *packet, uint32_t nonce) {
	packet[0] = REQUEST;
	packet[1] = 6;
	uint32_t id = htonl(nonce);
	memcpy(&packet[2], &id, sizeof(uint32_t));
	return packet[1];
}

void intercom_send_request(intercom_ctx *ctx, audio_packet *mp) {
	struct intercom_task *data = snap_alloc0(sizeof(struct intercom_task));
	data->packet = snap_alloc(sizeof(intercom_packet_op) + sizeof(tlv_request));

	data->packet_len = assemble_header(&((intercom_packet_op *)data->packet)->hdr, CLIENT_OPERATION, &nonce, ctx->nodeid);
	data->packet_len += assemble_request(&data->packet[data->packet_len], mp->nonce);
	log_debug("assembling request packet for nonce: %lu\n", mp->nonce);

	data->retries_left = snapctx.bufferms / REQUEST_RETRY_INTERVAL_MS;

	data->recipient = snap_alloc_aligned(sizeof(struct in6_addr), sizeof(struct in6_addr));
	memcpy(data->recipient, &ctx->serverip, sizeof(struct in6_addr));

	post_task(&snapctx.taskqueue_ctx, 0, 0, request_task, free_intercom_task, data);
}


void remove_old_data_from_queue(intercom_ctx *ctx) {
	struct timespec oldest_play_at;
	struct timespec ctime;

	pcmChunk *oldest = pqueue_peek(ctx->receivebuffer);
	if (oldest) {
		obtainsystime(&ctime);
		oldest_play_at = chunk_get_play_at(oldest);
		while (oldest && oldest_play_at.tv_sec && timespec_cmp(ctime, oldest_play_at) > 0) {
			log_verbose("removing old chunk\n");
			pcmChunk *p = pqueue_dequeue(ctx->receivebuffer);
			chunk_free_members(p);
			free(p);
			pcmChunk *oldest = pqueue_peek(ctx->receivebuffer);
			oldest_play_at = chunk_get_play_at(oldest);
		}
	}
	log_debug("queue size %d/%d, oldest chunk in queue to be played at %s, current time %s\n", ctx->receivebuffer->size,
		  ctx->receivebuffer->capacity, oldest ? print_timespec(&oldest_play_at) : 0, oldest ? print_timespec(&ctime) : 0);
}

void intercom_put_chunk(intercom_ctx *ctx, pcmChunk *chunk) {
	remove_old_data_from_queue(ctx);

	pqueue_enqueue(ctx->receivebuffer, chunk);
	underrun = false;

	log_debug("placed chunk in receivebuffer\n");
}

bool is_next_chunk(uint32_t seq) {
	intercom_ctx *ctx = &snapctx.intercom_ctx;

	return (((seq == 0) && (ctx->lastreceviedseqno == NONCE_MAX)) ||
		((seq - 1) == ctx->lastreceviedseqno && ctx->lastreceviedseqno != NONCE_MAX));
};

bool already_requesting(intercom_ctx *ctx, uint32_t nonce) {
	audio_packet ap = {.nonce = nonce};
	audio_packet *already_requesting = VECTOR_LSEARCH(&ap, ctx->missing_packets, cmp_audiopacket);
	return !!already_requesting;
}

bool intercom_handle_audio(intercom_ctx *ctx, intercom_packet_audio *packet, int packet_len) {
	// TODO: Implementing TLV format for audio data will de-couple readms from the packet size.
	uint8_t *packetpointer = &((uint8_t *)packet)[sizeof(intercom_packet_audio)];

	pcmChunk *chunk = snap_alloc(sizeof(pcmChunk));
	pcmChunk *pchunk = (pcmChunk *)packetpointer;

	chunk_copy_meta(chunk, pchunk);
	chunk_ntoh(chunk);
	chunk->data = snap_alloc(chunk->size);

	int currentoffset = CHUNK_HEADER_SIZE + sizeof(intercom_packet_audio);
	memcpy(chunk->data, &((uint8_t *)packet)[currentoffset], chunk->size);

	size_t this_seqno = ntohl(packet->hdr.nonce);
	remove_request(this_seqno);  // we received the packet all right. Let's not request it again.

	// when buffer is empty, it may not have been initialized and thus the server may have adjusted its chunk size.
	log_debug("handling audio data\n");
	if (!(ctx->receivebuffer && ctx->receivebuffer->capacity)) {
		log_error("buffer not initialized. Old size: %lu\n", ctx->receivebuffer ? ctx->receivebuffer->capacity : 0);
		realloc_intercom_buffer_when_required(ctx, ntohs(packet->bufferms), chunk_getduration_ms(chunk));
	}

	struct timespec play_at = {
	    .tv_sec = chunk->play_at_tv_sec, .tv_nsec = chunk->play_at_tv_nsec,
	};

	log_verbose("read chunk from packet: %d samples: %d frame size: %d channels: %d packet_len: %d, play_at %s\n", chunk->size, chunk->samples,
		    chunk->frame_size, chunk->channels, packet_len, print_timespec(&play_at));

	struct timespec ctime;
	obtainsystime(&ctime);

	if (timespec_cmp(play_at, ctime) > 0) {
		if (!is_next_chunk(this_seqno)) {
			bool is_chunk_retransmit = this_seqno < ctx->lastreceviedseqno;
			bool is_chunk_far_in_future = (!is_chunk_retransmit) && this_seqno - ctx->lastreceviedseqno >= ctx->receivebuffer->capacity;

			if (!is_chunk_retransmit) {
				if (is_chunk_far_in_future) {
					log_error("WARN: huge loss of %lu packets detected. Resetting seqno. This will be audible\n",
						  this_seqno - ctx->lastreceviedseqno);
					ctx->lastreceviedseqno = this_seqno;
					realloc_intercom_buffer(ctx, ctx->receivebuffer->capacity);
				} else {
					log_error(
					    "Packet loss for %lu packets detected: Last received audio chunk had seqno %lu, we just received %lu.\n",
					    this_seqno - ctx->lastreceviedseqno - 1, ctx->lastreceviedseqno, this_seqno);

					// TODO: place multiple TLV for request into a single packet for more efficiency
					size_t max_requests = max(ctx->lastreceviedseqno, this_seqno - ctx->receivebuffer->capacity) + 1;
					for (size_t i = max_requests; i < this_seqno; ++i) {
						if (!already_requesting(ctx, i)) {
							audio_packet ap = {.nonce = i};
							VECTOR_ADD(snapctx.intercom_ctx.missing_packets, ap);
							limit_missing_packets(ctx, ctx->receivebuffer->capacity);
							intercom_send_request(ctx, &ap);
							log_verbose("requested packet with seqno: %lu\n", i);
						}
					}
				}
			}
		}

		log_debug("storing chunk\n");
		intercom_put_chunk(ctx, chunk);
		if (this_seqno > ctx->lastreceviedseqno)
			ctx->lastreceviedseqno = this_seqno;

		if (chunk->frame_size != snapctx.alsaplayer_ctx.frame_size || (chunk->channels != snapctx.alsaplayer_ctx.channels) ||
		    (chunk->samples != snapctx.alsaplayer_ctx.rate)) {
			log_error("chunk size is not equal to alsa init size - (re-)initializing with samples: %lu sample size: %d, channels %d\n",
				  chunk->samples, chunk->frame_size, chunk->channels);

			alsaplayer_remove_task(&snapctx.alsaplayer_ctx);
			alsaplayer_uninit(&snapctx.alsaplayer_ctx);
		}

		if (!snapctx.alsaplayer_ctx.initialized) {
			snapctx.alsaplayer_ctx.frame_size = chunk->frame_size;
			snapctx.alsaplayer_ctx.channels = chunk->channels;
			snapctx.alsaplayer_ctx.rate = chunk->samples;

			alsaplayer_init(&snapctx.alsaplayer_ctx);
			init_alsafd(&snapctx.alsaplayer_ctx);
		}
		return true;
	} else {
		log_error("discarding chunk %d (play at: %s) - too late, it is now %s.\n", this_seqno, print_timespec(&play_at),
			  print_timespec(&ctime));
		chunk_free_members(chunk);
		free(chunk);
		if (this_seqno > ctx->lastreceviedseqno) {
			ctx->lastreceviedseqno = this_seqno;
		}
		prune_missing_packets(ctx, this_seqno);
		return false;
	}
}

void intercom_handle_packet(intercom_ctx *ctx, uint8_t *packet, ssize_t packet_len, struct in6_addr *peer, uint16_t port) {
	intercom_packet_hdr *hdr = (intercom_packet_hdr *)packet;

	if (hdr->version == PACKET_FORMAT_VERSION) {
		if (intercom_recently_seen(ctx, hdr)) {
			if (already_requesting(ctx, hdr->nonce))
				log_error("DROPPING audio packet with id %lu which we have previously seen yet newly requested.\n", hdr->nonce);
			else 
				log_error("DROPPING audio packet with id %lu which we have previously seen.\n", hdr->nonce);
			return;
		}

		intercom_recently_seen_add(ctx, hdr);
		if (hdr->type == SERVER_OPERATION)
			intercom_handle_server_operation(ctx, (intercom_packet_sop *)packet, packet_len);
		if (hdr->type == AUDIO_DATA)
			intercom_handle_audio(ctx, (intercom_packet_audio *)packet, packet_len);

	} else {
		log_error(
		    "unknown packet with version %i received on intercom. Ignoring content and dropping the packet that could have "
		    "originated from: %s. This is a guess with current or previous positions of the originator\n",
		    hdr->version, print_ip((void *)&packet[6]));
	}
}

void intercom_reinit(void *d) {
	intercom_ctx *ctx = (intercom_ctx*) d;

	ctx->fd = socket(PF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (ctx->fd < 0)
		exit_error("creating socket for intercom on node-IP");

	struct sockaddr_in6 server_addr = {
	    .sin6_family = AF_INET6, .sin6_port = htons(ctx->port),
	};

	server_addr.sin6_addr = ctx->serverip;
	if (connect(ctx->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("connect socket to server-IP failed");
		exit(EXIT_FAILURE);
	}
	intercom_hello(ctx, &ctx->serverip, ctx->port);
}

void intercom_uninit(intercom_ctx *ctx) {
	free(((struct intercom_task*)(ctx->hello_task->data))->packet);
	free(((struct intercom_task*)(ctx->hello_task->data))->recipient);
	free(ctx->hello_task->data);
	drop_task(&snapctx.taskqueue_ctx, ctx->hello_task);
	close(ctx->fd);
	ctx->lastreceviedseqno = 0;

	while (pqueue_peek(ctx->receivebuffer)) {
		pcmChunk *p = pqueue_dequeue(ctx->receivebuffer);
		chunk_free_members(p);
		free(p);
	}
}

void intercom_init(intercom_ctx *ctx) {
	obtainrandom(&nonce, sizeof(uint32_t), 0);
	ctx->receivebuffer = NULL;

	VECTOR_INIT(ctx->recent_packets);
	VECTOR_INIT(ctx->missing_packets);

	intercom_reinit(ctx);
}

