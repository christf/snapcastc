#pragma once

#include <opus/opus.h>

typedef struct {
	OpusDecoder *decoder;
	OpusEncoder *encoder;

	uint32_t bitrate;
	int mss;

} opuscodec_ctx;

void opus_init_encoder(int mss);
void encode_opus_handle(pcmChunk *chunk);
void opus_init_decoder();
void decode_opus_handle(pcmChunk *chunk);


