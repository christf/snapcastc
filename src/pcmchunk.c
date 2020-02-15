#include "pcmchunk.h"
#include "alloc.h"
#include "opuscodec.h"
#include "snapcast.h"
#include "util.h"

#include <arpa/inet.h>
#include <time.h>
#include "timespec.h"

#define EMPTY_CHUNK_SIZE_MS 5

void get_emptychunk(pcmChunk *ret, unsigned int length_ms) {
	if (!ret)
		return;
	ret->size = ret->samples * length_ms * ret->channels * ret->frame_size / 1000;
	ret->play_at_tv_sec = 0L;
	ret->play_at_tv_nsec = 0L;
	ret->data = snap_alloc0(ret->size);
	ret->codec = CODEC_PCM;
	log_debug("generated chunks with size %d and length %lu ms\n", ret->size, length_ms);
}

struct timespec chunk_get_play_at(pcmChunk *chunk) {
	struct timespec ret = {};
	if (chunk) {
		ret.tv_sec = chunk->play_at_tv_sec;
		ret.tv_nsec = chunk->play_at_tv_nsec;
	}
	return ret;
}

int chunk_getduration_ms(pcmChunk *chunk) {
	chunk_decode(chunk); // chunk must be in pcm format to obtain the correct duration
	return (chunk->channels && chunk->frame_size && chunk->samples) ? 1000 * chunk->size / chunk->channels / chunk->frame_size / chunk->samples
									: 0;
}

bool chunk_is_empty(pcmChunk *c) { return !(c && c->play_at_tv_sec); }

bool chunk_decode(pcmChunk *c) {
	extern opuscodec_ctx opuscodec;
	if (c && c->codec == CODEC_OPUS) {
		decode_opus_handle(&opuscodec, c);
		return true;
	}
	else
		return false;
}

void chunk_ntoh(pcmChunk *chunk) {
	chunk->play_at_tv_sec = ntohl(chunk->play_at_tv_sec);
	chunk->play_at_tv_nsec = ntohl(chunk->play_at_tv_nsec);
	chunk->samples = ntohl(chunk->samples);
	chunk->size = ntohs(chunk->size);
}

void chunk_free_members(pcmChunk *chunk) {
	if (chunk) {
		free(chunk->data);
		chunk->play_at_tv_sec = 0;
		chunk->data = NULL;
		chunk->size = 0;
	}
}

void pcmchunk_shaveoff(pcmChunk *chunk, int frames) {
	// remove the first frames from a chunk.

	int removebytes = chunk->channels * chunk->frame_size * frames;
	uint8_t *ndata = snap_alloc(chunk->size - removebytes);
	memcpy(ndata, &chunk->data[removebytes], chunk->size - removebytes);
	free(chunk->data);
	chunk->data = ndata;
	chunk->size -= removebytes;

	struct timespec play_at = {.tv_sec = chunk->play_at_tv_sec, .tv_nsec = chunk->play_at_tv_nsec};

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

int chunk_cmp(pcmChunk *c1, pcmChunk *c2) {
	if (c1->play_at_tv_sec > c2->play_at_tv_sec)
		return 1;
	else if (c1->play_at_tv_sec < c2->play_at_tv_sec)
		return -1;
	else if (c1->play_at_tv_nsec > c2->play_at_tv_nsec)
		return 1;
	else if (c1->play_at_tv_nsec < c2->play_at_tv_nsec)
		return -1;

	return 0;
}

void chunk_copy_meta(pcmChunk *dest, const pcmChunk *src) {
	dest->play_at_tv_sec = src->play_at_tv_sec;
	dest->play_at_tv_nsec = src->play_at_tv_nsec;
	dest->samples = src->samples;
	dest->frame_size = src->frame_size;
	dest->channels = src->channels;
	dest->size = src->size;
	dest->codec = src->codec;
}
