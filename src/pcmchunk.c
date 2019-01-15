#include "pcmchunk.h"
#include "snapcast.h"
#include "util.h"

#include <arpa/inet.h>
#include <time.h>

void get_emptychunk(pcmChunk *ret) {
	struct timespec t = {};
	ret->samples = snapctx.alsaplayer_ctx.rate;
	ret->channels = snapctx.alsaplayer_ctx.channels;
	ret->frame_size = snapctx.alsaplayer_ctx.frame_size;
	ret->size = CHUNKSIZE;
	ret->play_at = t;
	memset(ret->data, 0, ret->size);
	log_error("returning silence\n");  // should we display a timestamp?
}

bool chunk_is_empty(pcmChunk *c) { return !(c->play_at.tv_sec > 0); }

void chunk_ntoh(pcmChunk *chunk) {
	chunk->play_at.tv_sec = ntohl(chunk->play_at.tv_sec);
	chunk->play_at.tv_nsec = ntohl(chunk->play_at.tv_nsec);
	chunk->samples = ntohl(chunk->samples);
	chunk->size = ntohs(chunk->size);
}

void chunk_hton(pcmChunk *chunk) {
	chunk->play_at.tv_sec = htonl(chunk->play_at.tv_sec);
	chunk->play_at.tv_nsec = htonl(chunk->play_at.tv_nsec);
	chunk->samples = htonl(chunk->samples);
	chunk->size = htons(chunk->size);
}
