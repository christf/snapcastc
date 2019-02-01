
#include "alloc.h"
#include "error.h"
#include "pcmchunk.h"
#include "snapcast.h"
#include "util.h"

#include "syscallwrappers.h"

// opus will encode / decode max 120ms
#define MAX_FRAMES (snapctx.samples * 12 / 100)

void decode_opus_handle(pcmChunk *chunk) {
	struct timespec ctime;
	obtainsystime(&ctime);
	log_error("starting decoder at %s\n", print_timespec(&ctime));
	uint8_t
	    *out[MAX_FRAMES * snapctx.alsaplayer_ctx.channels * snapctx.alsaplayer_ctx.frame_size];  // maximum for opus chunk: 60ms data at 48000:2:2
	int frames = opus_decode(snapctx.opuscodec_ctx.decoder, chunk->data, chunk->size, (opus_int16 *)out, MAX_FRAMES, 0);
	if (frames < 0) {
		log_error("decoder failed: %s\n", opus_strerror(frames));
		chunk->size = 0;
		return;
	}

	uint8_t *dout = snap_alloc(frames * chunk->channels * chunk->frame_size);
	free(chunk->data);
	chunk->data = dout;
	chunk->size = frames * chunk->channels * chunk->frame_size;
	chunk->codec = CODEC_PCM;
	memcpy(dout, out, chunk->size);
	log_debug("decode happened, adjusting chunk size: %d\n", chunk->size);
	obtainsystime(&ctime);
	log_error("starting decoder at %s\n", print_timespec(&ctime));
}

void encode_opus_handle(pcmChunk *chunk) {
	unsigned char out[snapctx.intercom_ctx.mtu - sizeof(intercom_packet_audio)];

	int frames = chunk->size / chunk->frame_size / chunk->channels;
	log_debug("encode opus: chunk: chunksize: %d %d %d %d\n", chunk->size, chunk->samples, chunk->frame_size, frames);

	int nbBytes = opus_encode(snapctx.opuscodec_ctx.encoder, (opus_int16 *)chunk->data, frames, out, snapctx.opuscodec_ctx.mss);
	if (nbBytes < 0) {
		log_error("encode failed: %s\n", opus_strerror(nbBytes));
	}

	chunk->size = nbBytes;
	memcpy(chunk->data, out, nbBytes);
	chunk->codec = CODEC_OPUS;

	log_debug("encode happened, adjusting chunk size: %d\n", chunk->size);
}

void opus_init_decoder() {
	int err = 0;
	snapctx.opuscodec_ctx.decoder = opus_decoder_create(snapctx.samples, snapctx.channels, &err);
	if (err < 0) {
		exit_error("failed to create decoder: %s\n", opus_strerror(err));
	}
}

void opus_init_encoder(int mss) {
	int err = 0;
	snapctx.opuscodec_ctx.mss = mss;
	snapctx.opuscodec_ctx.encoder = opus_encoder_create(snapctx.samples, snapctx.channels, OPUS_APPLICATION_AUDIO, &err);

	if (err < 0)
		exit_error("failed to create an encoder: %s\n", opus_strerror(err));

	if (opus_encoder_ctl(snapctx.opuscodec_ctx.encoder, OPUS_SET_BITRATE(snapctx.opuscodec_ctx.bitrate)) < 0)
		exit_error("failed to set bitrate: %s\n", opus_strerror(err));
}

void uninit() {
	opus_encoder_destroy(snapctx.opuscodec_ctx.encoder);
	opus_decoder_destroy(snapctx.opuscodec_ctx.decoder);
}
