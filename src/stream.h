#pragma once

#include "client.h"
#include "inputpipe.h"
#include "opuscodec.h"
#include "vector.h"
#include "packet_types.h"

#include <stdbool.h>

enum codec { PCM = 1, OPUS };
enum protocol { PIPE = 1 };

// this will be used in the client for decoding
opuscodec_ctx opuscodec;

typedef struct {
	int efd;
	int fd;
} streamtask;

typedef struct {
	inputpipe_ctx inputpipe;
	enum codec codec;
	enum protocol protocol;
	opuscodec_ctx opuscodec_ctx;

	client_vector clients;
	VECTOR(audio_packet) packet_buffer;

	uint32_t nonce;

	char *name;
	char *raw;
} stream;

stream *stream_find(const client_t *client);
stream *stream_find_fd(int fd);

int stream_free_members(stream *s);
bool stream_parse(stream *s, const char *raw);

