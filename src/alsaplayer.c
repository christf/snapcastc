#include "alsaplayer.h"
#include "alloc.h"
#include "snapcast.h"
#include "syscallwrappers.h"
#include "util.h"

#include <alsa/asoundlib.h>
#include <rubberband/rubberband-c.h>
#include <stdio.h>

#include "timespec.h"

#define PCM_DEVICE "default"
#define PERIOD_TIME 30000

// librubberband may get more interesting when compressing data. feeding 2048 samples at a time is out of question when using PCM, UDP and 16 Bit 2
// Channel
void adjust_speed_rubber(pcmChunk *chunk, double factor) {
	RubberBandState rbs = rubberband_new(chunk->samples, chunk->channels, RubberBandOptionProcessRealTime, factor, 1);

	uint16_t inframes = chunk->size / chunk->channels / chunk->frame_size;
	uint16_t outframes = chunk->size / chunk->channels / chunk->frame_size * factor;

	rubberband_set_expected_input_duration(rbs, snapctx.alsaplayer_ctx.frames);
	unsigned int required = rubberband_get_samples_required(rbs);
	log_error("required samples: %d, have: %d, alsa frames: %ld\n", required, inframes, snapctx.alsaplayer_ctx.frames);
	rubberband_set_max_process_size(rbs, inframes);

	rubberband_process(rbs, (const float *const *)chunk->data, inframes, 0);
	int nb_samples = rubberband_available(rbs);
	//	rubberband_retrieve(rbs, (float* *const)out, outframes);

	//	log_error("len: %d, olen: %d odone: %d sox-error: %d\n", chunk->size, olen, odone, error);
	chunk->size = outframes * chunk->channels * chunk->frame_size;
}

void adjust_speed_simple(pcmChunk *chunk, double factor) {
	// stretch by removing or inserting a single frame at the end of the chunk.
	// Beware: This reduces ability to sync when larger chunks are used and the logic does not consider the factor at all beyond it being larger
	// or smaller than 1.
	if (factor == 1)
		return;
	else if (factor < 1) {
		chunk->size = chunk->size - chunk->channels * chunk->frame_size;
	} else {
		uint8_t *out = snap_alloc(chunk->size + chunk->channels * chunk->frame_size);
		memcpy(out, chunk->data, chunk->size);
		memcpy(&out[chunk->size], &chunk->data[chunk->size - chunk->channels * chunk->frame_size], chunk->channels * chunk->frame_size);
		free(chunk->data);
		chunk->data = out;
		chunk->size = chunk->size + chunk->channels * chunk->frame_size;
	}
}

void adjust_speed(pcmChunk *chunk, double factor) {
	// TODO: should we be able to select this via cli option?
	adjust_speed_simple(chunk, factor);
}

int getchunk(pcmChunk *p, size_t delay_frames) {
	double factor = 1;
	struct timespec ctime;
	obtainsystime(&ctime);
	struct timespec ts = ctime;

	int near_ms = 1;
	int not_even_close_ms = 250;

	struct timespec nextchunk_playat = intercom_get_time_next_audiochunk(&snapctx.intercom_ctx);

	size_t delay_ms_alsa = delay_frames * 1000 / snapctx.alsaplayer_ctx.rate;
	ts = timeAddMs(&ts, delay_ms_alsa);

	timediff tdiff = timeSub(&ts, &nextchunk_playat);

	bool is_near = (tdiff.time.tv_sec == 0 && tdiff.time.tv_nsec < near_ms * 1000000L);
	if (snapctx.alsaplayer_ctx.playing || ((!snapctx.alsaplayer_ctx.playing) && tdiff.sign > 0) || is_near) {
		intercom_getnextaudiochunk(&snapctx.intercom_ctx, p);
		if (chunk_is_empty(p)) {
			snapctx.alsaplayer_ctx.empty_chunks_in_row++;
			if (snapctx.alsaplayer_ctx.empty_chunks_in_row > 5)
				snapctx.alsaplayer_ctx.playing = false;
		} else {
			snapctx.alsaplayer_ctx.playing = true;
			snapctx.alsaplayer_ctx.empty_chunks_in_row = 0;
			reschedule_task(&snapctx.taskqueue_ctx, snapctx.alsaplayer_ctx.close_task, (1.2 * snapctx.bufferms) / 1000,
					(int)(1.2 * snapctx.bufferms) % 1000);
		}
	} else
		get_emptychunk(p);

	if (!is_near) {
		factor = (1 - (tdiff.sign * ((double)(tdiff.time.tv_sec * 1000 + tdiff.time.tv_nsec / 1000000L) / 1000)));

		// TODO: this factor already works pretty well, there are two issues though:
		// * we may end up in a local optimum while just playing one frame less or one frame more might be optimal.

		bool not_even_close = (tdiff.time.tv_sec == 0 && tdiff.time.tv_nsec < not_even_close_ms * 1000000L);
		if (!not_even_close) {
			log_debug("Timing is not even close, replacing chunk data with silence!\n");
			if (tdiff.sign < 0) {  // we are way ahead, play silence
				memset(p->data, 0, p->size);
				p->play_at_tv_sec = 0;

				snapctx.alsaplayer_ctx.playing = false;
				snapctx.alsaplayer_ctx.empty_chunks_in_row = 0;
			} else {  // we are way behind, drop chunk to allow syncing
				p->size = 0;
				p->play_at_tv_sec = 0;
				free(p->data);
				return -1;
			}
		}
	}

	//	if (!chunk_is_empty(p))  // save CPU and do not resample, when chunk contains only silence
	adjust_speed(p, factor);

	// TODO adjust volume

	log_verbose("status: %d factor: %f chunk: chunksize: %d current time: %s, play_at: %s difference: %s sign: %d\n",
		    snapctx.alsaplayer_ctx.playing, factor, p->size, print_timespec(&ctime), print_timespec(&ts), print_timespec(&tdiff.time),
		    tdiff.sign);

	return p->size;
}

void alsaplayer_handle(alsaplayer_ctx *ctx) {
	unsigned int pcm;
	snd_pcm_sframes_t delayp = 0;
	pcmChunk chunk;

	if (snd_pcm_delay(ctx->pcm_handle, &delayp) < 0)
		log_error("could not obtain pcm delay\n");

	int ret;

	if (!ctx->overflow)
		ret = getchunk(&chunk, delayp);
	else {
		chunk = *ctx->overflow;
		ret = chunk.size;
		log_debug("writing %d overflow bytes (%d frames) to PCM %d\n", chunk.size, chunk.size / chunk.channels / chunk.frame_size);
	}

	if (ret == 0) {
		log_error("end of data\n");
	} else if (ret == -1) {  // dropping chunk
		return;
	}

	if ((pcm = snd_pcm_writei(ctx->pcm_handle, chunk.data, chunk.size / chunk.channels / chunk.frame_size)) == -EPIPE) {
		log_error("XRUN.\n");
		snd_pcm_prepare(ctx->pcm_handle);
	} else if (pcm < 0) {
		log_error("ERROR. Can't write to PCM device. %s, snd_pcm_recover(%d)\n", snd_strerror(pcm),
			  (int)snd_pcm_recover(ctx->pcm_handle, pcm, 0));
	} else if (pcm < chunk.size / ctx->channels / ctx->frame_size) {
		if (!ctx->overflow) {
			ctx->overflow = snap_alloc(sizeof(pcmChunk));
			chunk_copy_meta(ctx->overflow, &chunk);
			ctx->overflow->data = chunk.data;
		}
		pcmchunk_shaveoff(ctx->overflow, pcm);

		log_debug("----- NO ERROR ----  %d/%d bytes to pcm - splitting chunk to write the rest at a later stage\n", pcm,
			  chunk.size / ctx->channels / ctx->frame_size);
	} else if (pcm == chunk.size / ctx->channels / ctx->frame_size) {
		chunk_free_members(&chunk);
		free(ctx->overflow);
		ctx->overflow = NULL;
	}
}

void alsaplayer_uninit_task(void *d) {
	log_verbose("unititializing alsa after timeout\n");
	alsaplayer_uninit(&snapctx.alsaplayer_ctx);
}

void alsaplayer_pcm_list() {
	void **hints, **n;
	char *name, *descr, *io;
	pcm pcmDevice;

	if (snd_device_name_hint(-1, "pcm", &hints) < 0)
		return;
	n = hints;
	size_t idx = 0;
	while (*n != NULL) {
		name = snd_device_name_get_hint(*n, "NAME");
		descr = snd_device_name_get_hint(*n, "DESC");
		io = snd_device_name_get_hint(*n, "IOID");
		if (io != NULL && strcmp(io, "Output") != 0)
			goto __end;
		pcmDevice.name = name;

		if (descr == NULL) {
			pcmDevice.description = "";
		} else {
			pcmDevice.description = descr;
		}
		pcmDevice.id = idx++;
		log_error("PCM DEVICE name: %s id: %d description: %s\n", pcmDevice.name, pcmDevice.id, pcmDevice.description);

	__end:
		if (name != NULL)
			free(name);
		if (descr != NULL)
			free(descr);
		if (io != NULL)
			free(io);
		++n;
	}
	snd_device_name_free_hint(hints);
}

void alsaplayer_uninit(alsaplayer_ctx *ctx) {
	if (!ctx->initialized)
		return;
	snd_pcm_drain(ctx->pcm_handle);
	snd_pcm_close(ctx->pcm_handle);
	ctx->initialized = ctx->playing = false;
	free(ctx->ufds);

	for (int i = 0; i < ctx->pollfd_count; ++i) {
		ctx->main_poll_fd[i].fd = -(ctx->main_poll_fd[i]).fd;
	}
}

void init_alsafd(alsaplayer_ctx *ctx) {
	for (int i = 0; i < ctx->pollfd_count; i++) {
		struct pollfd *pfd = &snapctx.alsaplayer_ctx.ufds[i];
		ctx->main_poll_fd[i].fd = pfd->fd;
		ctx->main_poll_fd[i].events = POLLIN;
		ctx->main_poll_fd[i].revents = 0;
	}
}

void adjustVolume(unsigned char *buffer, size_t count, double volume) {
	for (size_t n = 0; n < count; ++n) buffer[n] = (buffer[n]) * volume;
}

void alsaplayer_init(alsaplayer_ctx *ctx) {
	unsigned int pcm, tmp;
	int err;

	ctx->empty_chunks_in_row = 0;
	ctx->playing = false;
	ctx->overflow = NULL;

	if (ctx->initialized)
		return;

	ctx->close_task = post_task(&snapctx.taskqueue_ctx, (snapctx.bufferms * 1.2) / 1000, (int)(snapctx.bufferms * 1.2) % 1000,
				    alsaplayer_uninit_task, NULL, NULL);

	int buff_size;

	ctx->pcm_handle = NULL;
	if ((pcm = snd_pcm_open(&ctx->pcm_handle, ctx->pcm.name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0)
		log_error("ERROR: Cannot open \"%s\" PCM device. %s\n", PCM_DEVICE, snd_strerror(pcm));

	snd_pcm_hw_params_malloc(&ctx->params);
	snd_pcm_hw_params_any(ctx->pcm_handle, ctx->params);

	if ((pcm = snd_pcm_hw_params_set_access(ctx->pcm_handle, ctx->params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
		log_error("ERROR: Can't set interleaved mode. %s\n", snd_strerror(pcm));

	snd_pcm_format_t snd_pcm_format;
	if (ctx->frame_size == 1)
		snd_pcm_format = SND_PCM_FORMAT_S8;
	else if (ctx->frame_size == 2)
		snd_pcm_format = SND_PCM_FORMAT_S16_LE;
	else if (ctx->frame_size == 3)
		snd_pcm_format = SND_PCM_FORMAT_S24_LE;
	else if (ctx->frame_size == 4)
		snd_pcm_format = SND_PCM_FORMAT_S32_LE;
	else
		exit_error("unsupported format\n");

	if ((pcm = snd_pcm_hw_params_set_format(ctx->pcm_handle, ctx->params, snd_pcm_format)) < 0)
		log_error("ERROR: Can't set format. %s\n", snd_strerror(pcm));

	if ((pcm = snd_pcm_hw_params_set_format(ctx->pcm_handle, ctx->params, snd_pcm_format)) < 0)
		log_error("ERROR: Can't set format. %s\n", snd_strerror(pcm));

	if ((pcm = snd_pcm_hw_params_set_channels(ctx->pcm_handle, ctx->params, ctx->channels)) < 0)
		log_error("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));

	if ((pcm = snd_pcm_hw_params_set_rate_near(ctx->pcm_handle, ctx->params, &ctx->rate, 0)) < 0)
		log_error("ERROR: Can't set rate. %s\n", snd_strerror(pcm));

	snd_pcm_hw_params_get_period_time_max(ctx->params, &tmp, 0);
	if (tmp > PERIOD_TIME)
		tmp = PERIOD_TIME;

	unsigned int buffer_time = 4 * tmp;
	snd_pcm_hw_params_set_period_time_near(ctx->pcm_handle, ctx->params, &tmp, 0);
	snd_pcm_hw_params_set_buffer_time_near(ctx->pcm_handle, ctx->params, &buffer_time, 0);

	if ((pcm = snd_pcm_hw_params(ctx->pcm_handle, ctx->params)) < 0)
		log_error("ERROR: Can't set hardware parameters. %s\n", snd_strerror(pcm));

	log_verbose("PCM name: '%s'\n", snd_pcm_name(ctx->pcm_handle));
	log_verbose("PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(ctx->pcm_handle)));

	snd_pcm_hw_params_get_channels(ctx->params, &tmp);
	log_verbose("channels: %i ", tmp);

	snd_pcm_hw_params_get_rate(ctx->params, &tmp, 0);
	log_verbose("rate: %d bps\n", tmp);

	snd_pcm_hw_params_get_period_size(ctx->params, &ctx->frames, 0);
	log_verbose("frames: %ld\n", ctx->frames);

	buff_size = ctx->frames * ctx->channels * ctx->frame_size /* 2 -> sample size */;
	log_verbose("alsa requested buff_size: %d\n", buff_size);

	snd_pcm_hw_params_get_period_time(ctx->params, &tmp, NULL);
	log_verbose("period time: %d\n", tmp);

	ctx->pollfd_count = snd_pcm_poll_descriptors_count(ctx->pcm_handle);
	assert(ctx->pollfd_count > 0);

	log_verbose("pollfd_count: %d\n", ctx->pollfd_count);
	ctx->ufds = snap_alloc(ctx->pollfd_count * sizeof(struct pollfd));

	if ((err = snd_pcm_poll_descriptors(ctx->pcm_handle, ctx->ufds, ctx->pollfd_count)) < 0) {
		exit_error("Unable to obtain poll descriptors for playback: %s\n", snd_strerror(err));
	}

	snd_pcm_sw_params_alloca(&ctx->swparams);

	snd_pcm_sw_params_current(ctx->pcm_handle, ctx->swparams);

	snd_pcm_sw_params_set_avail_min(ctx->pcm_handle, ctx->swparams, ctx->frames);

	snd_pcm_sw_params_set_start_threshold(ctx->pcm_handle, ctx->swparams, ctx->frames);
	// enable period events when requested
	//	snd_pcm_sw_params_set_period_event(ctx->pcm_handle, ctx->swparams, 1);

	snd_pcm_sw_params(ctx->pcm_handle, ctx->swparams);
	ctx->initialized = true;
}

bool is_alsafd(const int fd, const alsaplayer_ctx *ctx) {
	for (int i = 0; i < ctx->pollfd_count; i++) {
		struct pollfd *pfd = &ctx->ufds[i];

		if (fd == pfd->fd)
			return true;
	}

	return false;
}
