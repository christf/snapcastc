#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

typedef struct {
	struct timespec play_at;
	uint32_t samples;
	uint8_t frame_size;
	uint8_t channels;
	uint16_t size;
	uint8_t *data;
} pcmChunk;

void chunk_copy_meta(pcmChunk *dest, pcmChunk *src);
void get_emptychunk(pcmChunk *ret);
bool chunk_is_empty(pcmChunk *c);
void chunk_hton(pcmChunk *chunk);
void chunk_ntoh(pcmChunk *chunk);
