#pragma once

#include "inputpipe.h"

#define STREAM_NAME_LENGTH 64
#define URI_LENGTH 1024

enum codec { PCM = 0, OPUS };

typedef struct {
	int efd;
	int fd;
} streamtask;

typedef struct {
	inputpipe_ctx inputpipe;
	enum codec codec;
	char name[STREAM_NAME_LENGTH];
	char raw[URI_LENGTH];
} stream;

