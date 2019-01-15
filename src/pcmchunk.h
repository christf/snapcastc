#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

// TODO: move this to parameters of stream
#define FREQUENCY 48000
#define CHANNELS 2
#define SAMPLESIZE 2

// read the corresponding data for this amount of ms from input pipe 5 is a good value for PCM, for data to be encoded, go with 25
//#define READMS 25
#define READMS 5

// CHUNKSIZE - one chunk must fit inside a single packet.
// 5ms of PCM data for 48kHz are encoded with 980 Byte, for 44100Hz 882 Byte are used.
// for 128kbit/s encoded data, we can fit 80ms into a single UDP packet which coresponds to 1310.72 Byte and
#define CHUNKSIZE (FREQUENCY * CHANNELS * SAMPLESIZE / (1000 / READMS))

typedef struct {
	struct timespec play_at;
	uint32_t samples;
	uint8_t frame_size;
	uint8_t channels;
	uint16_t size;
	char data[CHUNKSIZE];
} pcmChunk;

void get_emptychunk(pcmChunk *ret);
bool chunk_is_empty(pcmChunk *c);
void chunk_hton(pcmChunk *chunk);
void chunk_ntoh(pcmChunk *chunk);
