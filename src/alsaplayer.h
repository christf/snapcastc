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
#pragma once

#include "pcmchunk.h"
#include "taskqueue.h"

#include <alsa/asoundlib.h>
#include <stdbool.h>
#include <sys/types.h>

#define NOT_EVEN_CLOSE_MS 30

typedef struct {
	char *name;
	int id;
	char *description;
} pcm;

typedef struct {
	long mixer_min;
	long mixer_max;

	char *card;
	char *mixer;

	// frame = samples (sample_size) * #channels
	unsigned int rate;
	int channels;
	int frame_size;

	snd_pcm_t *pcm_handle;
	snd_pcm_hw_params_t *params;
	snd_pcm_uframes_t frames;
	snd_pcm_sw_params_t *swparams;

	snd_mixer_t *mixer_handle;
	snd_mixer_elem_t *mixer_elem;

	int pollfd_count;
	struct pollfd *ufds;
	struct pollfd *main_poll_fd;

	uint32_t latency_ms;

	pcmChunk *overflow;

	bool initialized;
	bool playing;
	bool is_softvol;
	uint8_t softvol_level;
	size_t empty_chunks_in_row;
	taskqueue_t *close_task;
	pcm pcm;
} alsaplayer_ctx;

void alsaplayer_handle(alsaplayer_ctx *ctx);
void alsaplayer_init(alsaplayer_ctx *ctx);
void alsaplayer_uninit(alsaplayer_ctx *ctx);
void alsaplayer_remove_task(alsaplayer_ctx *ctx);
void init_alsafd();
uint8_t obtain_volume(alsaplayer_ctx *ctx);
void alsaplayer_pcm_list();
void adjustVolume(alsaplayer_ctx *ctx, uint8_t volume);
bool is_alsafd(const int fd, const alsaplayer_ctx *ctx);
