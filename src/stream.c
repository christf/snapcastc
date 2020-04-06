#include "stream.h"
#include "alloc.h"
#include "intercom_common.h"
#include "snapcast.h"
#include "util.h"
#include "inputpipe.h"

#include "clientmgr.h"

#include <stdlib.h>
#include <string.h>

#define PROTOCOL_DELIMITER "://"

// TODO: get rid of these hard-coded values. This is an accident waiting to happen
//
// this is the overhead of ipv6
#define IPOVERHEAD_BYTES 40
#define UDPOVERHEAD_BYTES 8

int stream_free_members(stream *s) {
	free(s->inputpipe.fname);
	free(s->name);
	free(s->raw);
	return 0;
}

int parse_codec(const char *codec) {
	if (!strcmp(codec, "opus"))
		return OPUS;
	if (!strcmp(codec, "pcm"))
		return PCM;
	return INVALID;
}

stream *stream_find(const client_t *client) {
	for (int s = 0; s < VECTOR_LEN(snapctx.streams); s++) {
		stream *stream = &VECTOR_INDEX(snapctx.streams, s);
		if (get_client(stream, client->id))
			return stream;
	}
	return NULL;
}

stream *stream_find_name(const char *name) {
	for (int i = VECTOR_LEN(snapctx.streams) - 1; i >= 0; --i) {
		stream *s = &VECTOR_INDEX(snapctx.streams, i);
		if (!strncmp(s->name, name, strlen(s->name)))
			return s;
	}
	return NULL;
}

stream *stream_find_fd(int fd) {
	for (int i = VECTOR_LEN(snapctx.streams) - 1; i >= 0; --i) {
		stream *s = &VECTOR_INDEX(snapctx.streams, i);
		if (fd == s->inputpipe.fd)
			return s;
	}
	return NULL;
}

bool stream_parse(stream *s, const char *raw) {
	char *token = NULL;
	char *value = NULL;
	char *optionctx = NULL;
	char *valuectx = NULL;

	s->raw = snap_strdup(raw);

	if (!strncmp(raw, "pipe", 4))
		s->protocol = PIPE;
	else {
		log_error("no known protocol found when parsing input stream %s\n", raw);
		return false;
	}

	token = strtok_r(strstr(s->raw, PROTOCOL_DELIMITER) + strlen(PROTOCOL_DELIMITER) * sizeof(char), "?", &optionctx);

	s->inputpipe.fname = snap_strdup(token);

	log_debug("found input: %s\n", s->inputpipe.fname);

	while (!!token) {
		token = strtok_r(NULL, "&", &optionctx);
		value = strtok_r(token, "=", &valuectx);
		value = strtok_r(NULL, "=", &valuectx);

		if (token) {
			log_debug("section found: %s (%s)\n", token, value);
			if (!strcmp(token, "buffer_ms"))
				s->inputpipe.read_ms = atol(value);
			else if (!strcmp(token, "codec"))
				s->codec = parse_codec(value);
			else if (!strcmp(token, "name"))
				s->name = snap_strdup(value);
			else if (!strcmp(token, "sampleformat")) {
				valuectx = NULL;
				value = strtok_r(value, ":", &valuectx);
				s->inputpipe.samples = atol(value);
				value = strtok_r(NULL, ":", &valuectx);
				s->inputpipe.samplesize = atol(value) / 8;
				value = strtok_r(NULL, ":", &valuectx);
				s->inputpipe.channels = atol(value);
			} else if (!strcmp(token, "timeout_ms"))
				s->inputpipe.pipelength_ms = atoi(value);
			else {
				log_error("unknown option - bailing out\n");
				return false;
			}
		}
	}

	strcpy(s->raw, raw);
	log_debug("raw: %s\n", s->raw);

	s->opuscodec_ctx.bitrate = 96000;  // set default
	opus_init_encoder(&s->opuscodec_ctx,
			  snapctx.intercom_ctx.mtu - IPOVERHEAD_BYTES - sizeof(intercom_packet_hdr) - CHUNK_HEADER_SIZE - UDPOVERHEAD_BYTES,
			  s->inputpipe.samples, s->inputpipe.channels);
	return true;
}

void stream_client_add(stream *s, client_t *c) {
	VECTOR_ADD(s->clients, *c);
	log_verbose("added client %zu(%s) to stream %s\n", c->id, c->name, s->name);
	if ((VECTOR_LEN(s->clients) == 1) && (s->inputpipe.state == IDLE)) {
		// TODO: it would be excellent to re-set the timestamps in the buffer allowing playback where we left off if no other client was
		// active
		if (s->inputpipe.resume_task)
			drop_task(&snapctx.taskqueue_ctx, s->inputpipe.resume_task);
		s->inputpipe.resume_task = NULL;
		inputpipe_resume_read(s);
	}
}

void stream_client_remove(stream *s, client_t *c) {
	int i = VECTOR_GETINDEX(s->clients, c);
	VECTOR_DELETE(s->clients, i);

	if (!VECTOR_LEN(s->clients))
		inputpipe_hold(&s->inputpipe);
}

void stream_init(stream *s) {
	VECTOR_INIT(s->clients);
	VECTOR_INIT(s->packet_buffer);
	s->nonce = 0;
	s->initialized = true;
}

