/*
   Copyright (c) 2018, Christof Schulze <christof@christofschulze.com>
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include "pcmchunk.h"
#include "taskqueue.h"

#include <stdbool.h>
#include <sys/types.h>

enum inputpipe_state { IDLE = 0, PLAYING };

typedef struct {
	struct snapctx *snapctx;
	char *fname;
	pcmChunk chunk;

	enum inputpipe_state state;

	ssize_t data_read;
	int fd;

	uint16_t chunksize;
	uint32_t pipelength_ms;
	struct timespec lastchunk;
	taskqueue_t *idle_task;
} inputpipe_ctx;

/** inputpipe_handle() will read the data from the audio pipe feeding data.
 * @param the inputpipe context
 * @return -1 on buffer overrun, 1 on chunk complete, 0 otherwise
 */
int inputpipe_handle(inputpipe_ctx *ctx);
void inputpipe_init(inputpipe_ctx *ctx);
