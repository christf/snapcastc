#include "pcmchunk.h"
#include "alloc.h"
#include "snapcast.h"
#include "util.h"

#include "timespec.h"

#include <arpa/inet.h>
#include <time.h>

void get_emptychunk(pcmChunk *ret) {
	struct timespec t = {};
	ret->samples = snapctx.alsaplayer_ctx.rate;
	ret->channels = snapctx.alsaplayer_ctx.channels;
	ret->frame_size = snapctx.alsaplayer_ctx.frame_size;
	ret->size = snapctx.alsaplayer_ctx.rate * snapctx.readms * snapctx.alsaplayer_ctx.channels * snapctx.alsaplayer_ctx.frame_size / 1000;
	ret->play_at_tv_sec = t.tv_sec;
	ret->play_at_tv_nsec = t.tv_nsec;
	ret->data = snap_alloc(ret->size);
	ret->codec = CODEC_PCM;
	memset(ret->data, 0, ret->size);
	log_verbose("created empty chunk\n");
}

bool chunk_is_empty(pcmChunk *c) { return !(c->play_at_tv_sec > 0); }

void chunk_ntoh(pcmChunk *chunk) {
	chunk->play_at_tv_sec = ntohl(chunk->play_at_tv_sec);
	chunk->play_at_tv_nsec = ntohl(chunk->play_at_tv_nsec);
	chunk->samples = ntohl(chunk->samples);
	chunk->size = ntohs(chunk->size);
}


void chunk_free_members(pcmChunk *chunk) {
	free(chunk->data);
	chunk->data = NULL;
}

void pcmchunk_shaveoff(pcmChunk *chunk, int frames) {
	// remove the first frames from a chunk.

	int removebytes = chunk->channels * chunk->frame_size * frames;
	uint8_t *ndata = snap_alloc(chunk->size - removebytes);
	memcpy(ndata, &chunk->data[removebytes], chunk->size - removebytes);
	free(chunk->data);
	chunk->data = ndata;
	chunk->size -= removebytes;

	struct timespec play_at = {
		.tv_sec = chunk->play_at_tv_sec,
		.tv_nsec = chunk->play_at_tv_nsec
	};

	play_at = timeAddMs(&play_at, frames * 1000 / chunk->samples);
	chunk->play_at_tv_sec = play_at.tv_sec;
	chunk->play_at_tv_nsec = play_at.tv_nsec;
}


void chunk_hton(pcmChunk *chunk) {
	chunk->play_at_tv_sec = htonl(chunk->play_at_tv_sec);
	chunk->play_at_tv_nsec = htonl(chunk->play_at_tv_nsec);
	chunk->samples = htonl(chunk->samples);
	chunk->size = htons(chunk->size);
}

void chunk_copy_meta(pcmChunk *dest, pcmChunk *src) {
	dest->play_at_tv_sec = src->play_at_tv_sec;
	dest->play_at_tv_nsec = src->play_at_tv_nsec;
	dest->samples = src->samples;
	dest->frame_size = src->frame_size;
	dest->channels = src->channels;
	dest->size = src->size;
	dest->codec = src->codec;
}
