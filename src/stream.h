#pragma once

#include "inputpipe.h"
#include "opuscodec.h"

#include <stdbool.h>

enum codec { PCM = 1, OPUS };
enum protocol { PIPE = 1 };

// this will only be used in the client for decoding
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

	char *name;
	char *raw;
} stream;

int stream_free_members(stream *s);
bool stream_parse(stream *s, const char *raw);

