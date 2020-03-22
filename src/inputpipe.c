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
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define COLD_START_OFFSET_MS 100

bool is_chunk_complete(inputpipe_ctx *ctx) { return (ctx->chunksize == ctx->data_read); }

void set_idle(void *d) {
	inputpipe_ctx *ctx = (inputpipe_ctx *)d;
	if (ctx->idle_task)
		drop_task(&snapctx.taskqueue_ctx, ctx->idle_task);
	log_verbose("INPUT_UNDERRUN... this will be audible.\n");
	ctx->state = IDLE;
	ctx->idle_task = NULL;
	ctx->data_read = 0;
}

int inputpipe_handle(inputpipe_ctx *ctx) {
	struct timespec ctime;
	obtainsystime(&ctime);

	struct timespec bufferfull = timeAddMs(&ctime, snapctx.bufferms * 95 / 100);
	struct timespec lastchunk_play_at = chunk_get_play_at(&ctx->chunk);
	bool buffer_full = (timespec_cmp(lastchunk_play_at, bufferfull) > 0);

	if (buffer_full)
		return -1;

	ssize_t count = read(ctx->fd, &(ctx->chunk.data)[ctx->data_read], ctx->chunksize - ctx->data_read);

	if (count == -1) {
		perror("reading input pipe failed");
	} else if (count == 0) {
		log_debug("read %d Bytes from inputpipe, data_read: %d chunksize: %d\n", count, ctx->data_read, ctx->chunksize);
		ctx->state = IDLE;
	} else if ((count > 0) && (ctx->state == IDLE)) {
		struct timespec start_playing_at;

		if (timespec_cmp(ctime, chunk_get_play_at(&ctx->chunk)) > 0) {
			start_playing_at = timeAddMs(&ctime, COLD_START_OFFSET_MS);
			log_verbose("Detected status change, resyncing timestamps. This will be audible.\n", ctx->state);
		} else {
			start_playing_at = timeAddMs(&lastchunk_play_at, ctx->read_ms);
		}

		ctx->data_read += count;
		ctx->state = PLAYING;
		ctx->chunk.play_at_tv_sec = start_playing_at.tv_sec;
		ctx->chunk.play_at_tv_nsec = start_playing_at.tv_nsec;

		log_debug("read chunk that is to be played at %s, current time %s\n",
			  print_timespec(&(struct timespec){.tv_sec = ctx->chunk.play_at_tv_sec, .tv_nsec = ctx->chunk.play_at_tv_nsec}),
			  print_timespec(&ctime));
		ctx->idle_task = post_task(&snapctx.taskqueue_ctx, snapctx.bufferms, snapctx.bufferms % 1000, set_idle, NULL, ctx);
	} else if ((count > 0) && ((ctx->state == PLAYING) || (ctx->state == THROTTLE))) {
		log_debug("read %d Bytes from inputpipe\n", count);
		ctx->data_read += count;
		reschedule_task(&snapctx.taskqueue_ctx, ctx->idle_task, snapctx.bufferms / 1000, snapctx.bufferms % 1000);
	}

	if (is_chunk_complete(ctx)) {
		int ret = 1;
		struct timespec play_at = chunk_get_play_at(&ctx->chunk);

		if (timespec_cmp(ctime, play_at) > 0) {
			log_verbose(
			    "Either this is the first chunk we read for the first client on an inputstream or we are horribly late when reading from "
			    "the pipes. Using current timestamp to play back current chunk.");
			play_at = timeAddMs(&ctime, COLD_START_OFFSET_MS);
			ret = 2;
		} else {
			play_at = timeAddMs(&play_at, ctx->read_ms);
		}

		timediff t = timeSub(&ctime, &play_at);
		ctx->chunk.play_at_tv_sec = play_at.tv_sec;
		ctx->chunk.play_at_tv_nsec = play_at.tv_nsec;

		log_verbose("read %lu Bytes of data from %s, last read was %lu, reader state: %s, to be played at %s, current time %s, diff: %c%s\n",
			    ctx->data_read, ctx->fname, count, print_inputpipe_status(ctx->state), print_timespec(&play_at), print_timespec(&ctime),
			    t.sign < 0 ? '-' : ' ', print_timespec(&t.time));
		ctx->chunk.size = ctx->data_read;
		ctx->chunk.frame_size = ctx->samplesize;
		ctx->chunk.samples = ctx->samples;
		ctx->chunk.channels = ctx->channels;
		ctx->chunk.codec = CODEC_PCM;
		ctx->data_read = 0;
		return ret;
	}
	return 0;
}

void inputpipe_resume_read(void *d) {
	log_debug("resuming reading from pipe\n");
	stream *s = (stream *)d;
	if (VECTOR_LEN(s->clients) && s->inputpipe.state != PLAYING) {
		if (!s->inputpipe.initialized) {
			inputpipe_init(&s->inputpipe);
		}
		add_fd(snapctx.efd, s->inputpipe.fd, EPOLLIN);
		s->inputpipe.state = PLAYING;
		s->inputpipe.resume_task = NULL;
	}
}

void inputpipe_hold(inputpipe_ctx *ctx) {
	if (ctx->state == PLAYING)
		del_fd(snapctx.efd, ctx->fd);
	set_idle(ctx);
}

void inputpipe_uninit(inputpipe_ctx *ctx) {
	inputpipe_hold(ctx);
	chunk_free_members(&ctx->chunk);
	close(ctx->fd);
	ctx->initialized = false;
}

void inputpipe_init(inputpipe_ctx *ctx) {
	uint32_t c = ctx->samples * ctx->channels * ctx->samplesize * ctx->read_ms / 1000;
	ctx->chunksize = c;
	ctx->chunk.size = ctx->chunksize;
	ctx->chunk.samples = ctx->samples;
	ctx->chunk.frame_size = ctx->samplesize;
	ctx->chunk.channels = ctx->channels;
	ctx->data_read = 0;
	ctx->chunk.data = snap_alloc(ctx->chunksize);
	ctx->fd = open(ctx->fname, O_RDONLY | O_NONBLOCK);
	if (ctx->fd == -1)
		exit_errno("unable to open input pipe %s", ctx->fname);
	else
		ctx->initialized = true;
}
