#include "opuscodec.h"
#include "alloc.h"
#include "error.h"
#include "pcmchunk.h"
#include "snapcast.h"
#include "util.h"

#include "syscallwrappers.h"

// opus knows of audio chunks of up to 120 milliseconds.
#define OPUS_MAX_CHUNK_LENGTH_MS 120


void decode_opus_handle(opuscodec_ctx *ctx, pcmChunk *chunk) {
	// The initialization really should happen somewhere else...
	if (!ctx->decoder) {
		log_error("initializing opus codec\n");
		opus_init_decoder(ctx, chunk->samples, chunk->channels);
	}

	struct timespec ctime;
	if (snapctx.debug) {
		obtainsystime(&ctime);
		log_debug("starting decoder at %s\n", print_timespec(&ctime));
	}

	int MAX_FRAMES = chunk->samples * OPUS_MAX_CHUNK_LENGTH_MS / 1000;
	uint8_t *out[MAX_FRAMES * chunk->channels * chunk->frame_size];
	int frames = opus_decode(ctx->decoder, chunk->data, chunk->size, (opus_int16 *)out, MAX_FRAMES, 0);
	if (frames <= 0) {
		pcmChunk empty;
		get_emptychunk(&empty);

		log_error("decoder failed: %s\n", opus_strerror(frames));
		chunk->data = empty.data;
		chunk->size = empty.size;
		chunk->codec = CODEC_PCM;
	} else {
		uint8_t *dout = snap_alloc(frames * chunk->channels * chunk->frame_size);
		free(chunk->data);
		chunk->data = dout;
		chunk->size = frames * chunk->channels * chunk->frame_size;
		chunk->codec = CODEC_PCM;
		memcpy(dout, out, chunk->size);
	}

	if (snapctx.debug) {
		obtainsystime(&ctime);
		log_debug("finished decoder at %s\n", print_timespec(&ctime));
	}
}

void encode_opus_handle(opuscodec_ctx *ctx, pcmChunk *chunk) {
	unsigned char out[snapctx.intercom_ctx.mtu - sizeof(intercom_packet_audio)];

	int frames = chunk->size / chunk->frame_size / chunk->channels;
	log_debug("encode opus: chunk: chunksize: %d %d %d %d\n", chunk->size, chunk->samples, chunk->frame_size, frames);

	int nbBytes = opus_encode(ctx->encoder, (opus_int16 *)chunk->data, frames, out, ctx->mss);
	if (nbBytes < 0) {
		log_error("encode failed: %s\n", opus_strerror(nbBytes));
	}

	chunk->size = nbBytes;
	memcpy(chunk->data, out, nbBytes);
	chunk->codec = CODEC_OPUS;

	log_debug("encode happened, adjusting chunk size: %d\n", chunk->size);
}

void opus_init_decoder(opuscodec_ctx *ctx, size_t samples, size_t channels) {
	int err = 0;
	ctx->decoder = opus_decoder_create(samples, channels, &err);
	if (err < 0) {
		exit_error("failed to create decoder: %s\n", opus_strerror(err));
	}
}

void opus_init_encoder(opuscodec_ctx *ctx,int mss, size_t samples, size_t channels) {
	int err = 0;
	ctx->mss = mss;
	ctx->encoder = opus_encoder_create(samples, channels, OPUS_APPLICATION_AUDIO, &err);

	if (err < 0)
		exit_error("failed to create an encoder: %s\nfor parameters: mss %lu samples %lu channels %lu\n", opus_strerror(err), mss, samples, channels);

	if (opus_encoder_ctl(ctx->encoder, OPUS_SET_BITRATE(ctx->bitrate)) < 0)
		exit_error("failed to set bitrate: %s\n", opus_strerror(err));
}

void uninit(opuscodec_ctx *ctx) {
	opus_encoder_destroy(ctx->encoder);
	opus_decoder_destroy(ctx->decoder);
	ctx->decoder = NULL;
	ctx->encoder = NULL;
}
