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

bool is_chunk_complete(inputpipe_ctx *ctx) { return (ctx->chunksize == ctx->data_read); }

void set_idle(void *d) {
	log_verbose("INPUT_UNDERRUN... this will be audible.\n");
	snapctx.inputpipe_ctx.state = IDLE;
	snapctx.inputpipe_ctx.idle_task = NULL;
	snapctx.inputpipe_ctx.data_read = 0;
}

int inputpipe_handle(inputpipe_ctx *ctx) {
	struct timespec ctime;
	obtainsystime(&ctime);

	struct timespec start_playing_at = timeAddMs(&ctime, snapctx.bufferms * 9 / 10);  // target 90% buffer fill-rate to allow for some negative clock drift.
	struct timespec bufferfull = timeAddMs(&ctime, snapctx.bufferms * 95 / 100 );
	bool buffer_full = (timespec_cmp(ctx->lastchunk, bufferfull) > 0);

	if (buffer_full)
		return -1;

	ssize_t count = read(ctx->fd, &(ctx->chunk.data)[ctx->data_read], ctx->chunksize - ctx->data_read);

	if (count == -1) {
		perror("reading input pipe failed");
	} else if (count == 0) {
		log_debug("read %d Bytes from inputpipe, data_read: %d chunksize: %d\n", count, ctx->data_read, ctx->chunksize);
		ctx->state = IDLE;
	} else if ((count > 0) && (ctx->state == IDLE)) {
		ctx->data_read += count;
		ctx->state = PLAYING;
		ctx->chunk.play_at_tv_sec = start_playing_at.tv_sec;
		ctx->chunk.play_at_tv_nsec = start_playing_at.tv_nsec;
		ctx->lastchunk.tv_sec = ctx->chunk.play_at_tv_sec;
		ctx->lastchunk.tv_nsec = ctx->chunk.play_at_tv_nsec;
		log_verbose("Detected status change, resyncing timestamps. This will be audible.\n", ctx->state);

		log_debug("read chunk that is to be played at %s, current time %s\n",
			  print_timespec(&(struct timespec){.tv_sec = ctx->chunk.play_at_tv_sec, .tv_nsec = ctx->chunk.play_at_tv_nsec}),
			  print_timespec(&ctime));
		ctx->idle_task = post_task(&snapctx.taskqueue_ctx, snapctx.bufferms, snapctx.bufferms % 1000, set_idle, NULL, &snapctx.efd);
	} else if ((count > 0) && (ctx->state == PLAYING)) {
		log_debug("read %d Bytes from inputpipe\n", count);
		ctx->data_read += count;
		reschedule_task(&snapctx.taskqueue_ctx, ctx->idle_task, snapctx.bufferms / 1000 , snapctx.bufferms % 1000 );
	}

	if (is_chunk_complete(ctx)) {
		struct timespec play_at;
		play_at.tv_sec = ctx->chunk.play_at_tv_sec;
		play_at.tv_nsec = ctx->chunk.play_at_tv_nsec;

		play_at = timeAddMs(&play_at, snapctx.readms);
		timediff t = timeSub(&ctime, &play_at);
		ctx->chunk.play_at_tv_sec = play_at.tv_sec;
		ctx->chunk.play_at_tv_nsec = play_at.tv_nsec;
		ctx->lastchunk = play_at;

		log_verbose(
		    "read %lu Bytes of data from %s, last read was %lu, reader state: %i, PLAYING: %i, IDLE: %i, to be played at %s, current time "
		    "%s, diff: %s\n",
		    ctx->data_read, ctx->fname, count, ctx->state, PLAYING, IDLE, print_timespec(&play_at), print_timespec(&ctime),
		    print_timespec(&t.time));
		ctx->chunk.size = ctx->data_read;
		ctx->chunk.frame_size = snapctx.frame_size;
		ctx->chunk.samples = snapctx.samples;
		ctx->chunk.channels = snapctx.channels;
		ctx->lastchunk.tv_sec = ctx->chunk.play_at_tv_sec;
		ctx->lastchunk.tv_nsec = ctx->chunk.play_at_tv_nsec;
		ctx->chunk.codec = CODEC_PCM;
		ctx->data_read = 0;
		return 1;
	}
	return 0;
}

void inputpipe_init(inputpipe_ctx *ctx) {
	uint32_t c = snapctx.samples * snapctx.channels * snapctx.frame_size * snapctx.readms / 1000;
	ctx->chunksize = c;
	ctx->chunk.data = snap_alloc(ctx->chunksize);
	ctx->chunk.size = ctx->chunksize;
	ctx->chunk.samples = snapctx.samples;
	ctx->chunk.frame_size = snapctx.frame_size;
	ctx->chunk.channels = snapctx.channels;
	ctx->data_read = 0;
	ctx->fd = open(ctx->fname, O_RDONLY | O_NONBLOCK);
}

