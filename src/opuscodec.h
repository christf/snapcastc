#pragma once

#include "pcmchunk.h"

#include <opus/opus.h>
#include <stdio.h>

typedef struct {
	OpusDecoder *decoder;
	OpusEncoder *encoder;

	uint32_t bitrate;
	int mss;

} opuscodec_ctx;

void opus_init_encoder(opuscodec_ctx *ctx, int mss, size_t samples, size_t channels);
void encode_opus_handle(opuscodec_ctx *ctx, pcmChunk *chunk);
void opus_init_decoder(opuscodec_ctx *ctx, size_t samples, size_t channels);
void decode_opus_handle(opuscodec_ctx *ctx, pcmChunk *chunk);
