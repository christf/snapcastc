#pragma once

#include "inputpipe.h"

#define STREAM_NAME_LENGTH 64

typedef struct {
	int efd;
	int fd;
} streamtask;

typedef struct {
	inputpipe_ctx inputpipe;
	char name[STREAM_NAME_LENGTH];
} stream;

