#include "snapcast.h"
#include "stream.h"
#include "alloc.h"
#include "util.h"
#include "intercom.h"

#include <stdlib.h>
#include <string.h>

#define PROTOCOL_DELIMITER "://"


// TODO: get rid of these hard-coded values. This is an accident waiting to happen
#define IPOVERHEAD_BYTES 40
#define UDPOVERHEAD_BYTES 8
#define PCMCHUNK_HEADER_SIZE 17


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
}

bool stream_parse(stream *s, const char *raw) {
	char *token = NULL;
	char *value  = NULL;
	char *optionctx  = NULL;
	char *valuectx = NULL;

	s->raw = strdup(raw);

	if (!strncmp(raw, "pipe", 4))
		s->protocol = PIPE;
	else {
		log_error("no known protocol found when parsing input stream %s\n", raw);
		return false;
	}

	token = strtok_r(strstr(s->raw, PROTOCOL_DELIMITER) + strlen(PROTOCOL_DELIMITER) * sizeof(char), "?", &optionctx);

	s->inputpipe.fname = strdup(token);

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
				s->name = strdup(value);
			else if (!strcmp(token, "sampleformat")) {
				valuectx = NULL;
				value = strtok_r(value, ":", &valuectx);
				s->inputpipe.samples = atol(value);
				value = strtok_r(NULL, ":", &valuectx);
				s->inputpipe.samplesize = atol(value) / 8;
				value = strtok_r(NULL, ":", &valuectx);
				s->inputpipe.channels = atol(value);
			}
			else if (!strcmp(token, "timeout_ms"))
				s->inputpipe.pipelength_ms = atoi(value);
			else {
				log_error("unknown option - bailing out\n");
				return false;
			}
		}
	}

	strcpy(s->raw, raw);
	log_debug("raw: %s\n", s->raw);

	s->opuscodec_ctx.bitrate = 96000; // set default
	opus_init_encoder(&s->opuscodec_ctx, snapctx.intercom_ctx.mtu - IPOVERHEAD_BYTES - sizeof(intercom_packet_hdr) - PCMCHUNK_HEADER_SIZE - UDPOVERHEAD_BYTES, s->inputpipe.samples, s->inputpipe.channels);
	return true;
}

