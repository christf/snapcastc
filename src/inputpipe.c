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

#include "inputpipe.h"
#include "alloc.h"
#include "pcmchunk.h"
#include "snapcast.h"
#include "syscallwrappers.h"
#include "timespec.h"
#include "util.h"

#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// #define PIPELENGTH_S 4096 * 1024 / CHUNKSIZE / (1000 / READMS)

int get_pipe_length(size_t chunksize) {
	// default linux pipe size is 4MB
	return (4 * 1024 * 1024) / chunksize / (1000 / snapctx.readms);
}

bool is_chunk_complete(inputpipe_ctx *ctx) { return (ctx->chunk.size == ctx->data_read); }

void set_idle(void *d) {
	log_verbose("INPUT_UNDERRUN... this will be audible.\n");
	snapctx.inputpipe_ctx.state = IDLE;
	snapctx.inputpipe_ctx.idle_task = NULL;
	snapctx.inputpipe_ctx.data_read = 0;
}

int inputpipe_handle(inputpipe_ctx *ctx) {
	struct timespec ctime;
	obtainsystime(&ctime);

	// do not over-read
	struct timespec readuntil = timeAddMs(&ctime, snapctx.bufferms / 4 * 5);  // target 80% buffer fill-rate
	bool buffer_full = (timespec_cmp(ctx->lastchunk, readuntil) > 0);

	log_debug("reading chunk until %s,  lastchunk was for %s, compare result: %i\n", print_timespec(&readuntil), print_timespec(&ctx->lastchunk),
		  timespec_cmp(ctx->lastchunk, readuntil));
	if (buffer_full)
		return -1;

	ssize_t count = read(ctx->fd, &(ctx->chunk.data)[ctx->data_read], ctx->chunksize - ctx->data_read);

	if (count == -1) {
		perror("reading input pipe failed");
	}
	else if (count == 0) {
		ctx->state = IDLE;
	} else if ((count > 0) && (ctx->state == IDLE)) {
		ctx->data_read += count;
		ctx->state = PLAYING;
		ctx->chunk.play_at_tv_sec = readuntil.tv_sec;
		ctx->chunk.play_at_tv_nsec = readuntil.tv_nsec;
		ctx->lastchunk.tv_sec = ctx->chunk.play_at_tv_sec;
		ctx->lastchunk.tv_nsec = ctx->chunk.play_at_tv_nsec;
		log_verbose("Detected status change, resyncing timestamps. This will be audible.\n", ctx->state);

		log_debug("read chunk that is to be played at %s, current time %s\n",
			  print_timespec(&(struct timespec){.tv_sec = ctx->chunk.play_at_tv_sec, .tv_nsec = ctx->chunk.play_at_tv_nsec}),
			  print_timespec(&ctime));
		ctx->idle_task = post_task(&snapctx.taskqueue_ctx, get_pipe_length(ctx->chunksize), 0, set_idle, NULL, &snapctx.efd);
	} else if ((count > 0 )&& (ctx->state == PLAYING)) {
		ctx->data_read += count;
		// when incrementing timestamp, do not rely on local clock as data data may and will be read at a speed different than playback.
		struct timespec play_at;
		play_at.tv_sec = ctx->chunk.play_at_tv_sec;
		play_at.tv_nsec = ctx->chunk.play_at_tv_nsec;

		play_at = timeAddMs(&play_at, snapctx.readms);
		ctx->chunk.play_at_tv_sec = play_at.tv_sec;
		ctx->chunk.play_at_tv_nsec = play_at.tv_nsec;

		timediff t = timeSub(&ctime, &play_at);
		log_debug("read chunk that is to be played at %s, current time %s, diff: %s\n", print_timespec(&play_at), print_timespec(&ctime),
			  print_timespec(&t.time));
		ctx->lastchunk = play_at;
		reschedule_task(&snapctx.taskqueue_ctx, ctx->idle_task, get_pipe_length(ctx->chunksize), 0);
	}

	if (is_chunk_complete(ctx)) {
		log_debug("read %lu Bytes of data from %s, last read was %lu, reader state: %i, PLAYING: %i, IDLE: %i\n", ctx->data_read, ctx->fname,
			  count, ctx->state, PLAYING, IDLE);
		print_packet(ctx->chunk.data, ctx->chunk.size);
		ctx->chunk.size = ctx->data_read;
		ctx->chunk.frame_size = snapctx.frame_size;
		ctx->chunk.samples = snapctx.samples;
		ctx->chunk.channels = snapctx.channels;
		ctx->data_read = 0;
		return 1;
	}
	return 0;
}

void inputpipe_init(inputpipe_ctx *ctx) {
	ctx->chunksize = snapctx.samples * snapctx.channels * snapctx.frame_size / (1000 / snapctx.readms);
	ctx->chunk.data = snap_alloc(ctx->chunksize);
	ctx->chunk.size = ctx->chunksize;
	ctx->chunk.samples = snapctx.samples;
	ctx->chunk.frame_size = snapctx.frame_size;
	ctx->chunk.channels = snapctx.channels;
	ctx->data_read = 0;
	ctx->fd = open(ctx->fname, O_RDONLY | O_NONBLOCK);
}
