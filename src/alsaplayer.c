#include "alsaplayer.h"
#include "alloc.h"
#include "snapcast.h"
#include "syscallwrappers.h"
#include "util.h"

#include <alsa/asoundlib.h>
#include <math.h>
#include <rubberband/rubberband-c.h>
#include <stdio.h>

#include <soxr.h>

#include "timespec.h"

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
	// int nb_samples = rubberband_available(rbs);
	//	rubberband_retrieve(rbs, (float* *const)out, outframes);

	//	log_error("len: %d, olen: %d odone: %d sox-error: %d\n", chunk->size, olen, odone, error);
	chunk->size = outframes * chunk->channels * chunk->frame_size;
}

void adjust_speed_soxr(pcmChunk *chunk, double factor) {
	double orate = chunk->samples * factor;
	size_t olen = (size_t)(chunk->size * orate / chunk->samples + .5);
	size_t odone;

	uint8_t *out = snap_alloc(factor * chunk->size);

	soxr_quality_spec_t quality_spec = soxr_quality_spec(SOXR_QQ, 0);   // TODO: make this configurable - Raspi: SOXR_QQ, desktop: SOXR_LQ
	soxr_io_spec_t io_spec = soxr_io_spec(SOXR_INT16_I, SOXR_INT16_I);  // TODO this should not be hard-coded.

	soxr_error_t error = soxr_oneshot(chunk->samples, orate, chunk->channels, chunk->data, chunk->size / chunk->channels / chunk->frame_size,
					  NULL, out, olen, &odone, &io_spec, &quality_spec, NULL);

	log_debug("len: %d, olen: %d odone: %d sox-error: %d\n", chunk->size, olen, odone, error);
	free(chunk->data);
	chunk->data = out;
	chunk->size = olen;
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
	// adjust_speed_simple(chunk, factor);
	adjust_speed_soxr(chunk, factor);
}

int max(int a, int b) {
	if (a > b)
		return a;
	return b;
}

void decode_first_input(void *d) {
	pcmChunk *p;
	intercom_peeknextaudiochunk(&snapctx.intercom_ctx, &p);
	chunk_decode(p);
}

int getchunk(pcmChunk *p, size_t delay_frames) {
	double factor = 1;
	struct timespec ctime;
	obtainsystime(&ctime);
	struct timespec ts_alsa_ready = ctime;

	int near_ms = 1;
	int not_even_close_ms;

	struct timespec nextchunk_playat = intercom_get_time_next_audiochunk(&snapctx.intercom_ctx);

	size_t delay_ms_alsa = delay_frames * 1000 / snapctx.alsaplayer_ctx.rate;

	ts_alsa_ready = timeAddMs(&ts_alsa_ready, delay_ms_alsa + snapctx.alsaplayer_ctx.latency_ms);

	timediff tdiff = timeSub(&ts_alsa_ready, &nextchunk_playat);

	bool is_near = timespec_isnear(&ts_alsa_ready, &nextchunk_playat, near_ms);
	bool attempting_start_and_overshot = ((!snapctx.alsaplayer_ctx.playing) && (timespec_cmp(ts_alsa_ready, nextchunk_playat) >= 0));

	if (snapctx.alsaplayer_ctx.playing || (attempting_start_and_overshot) || is_near) {
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

			chunk_decode(p);
			post_task(&snapctx.taskqueue_ctx, 0, 0, decode_first_input, NULL, NULL);
		}
	} else {
		get_emptychunk(p, timespec_isnear(&ts_alsa_ready, &nextchunk_playat, 120) ? tdiff.time.tv_nsec / 1000000L : 120);
	}

	if (!is_near) {

		if (chunk_getduration_ms(p))
			factor = (1 - (tdiff.sign * ((double)(tdiff.time.tv_sec * 1000 + tdiff.time.tv_nsec / 1000000L) / chunk_getduration_ms(p))));
		if (factor > 2)
			factor = 2;
		if (factor < 0.5)
			factor = 0.5;

		not_even_close_ms = max(NOT_EVEN_CLOSE_MS, chunk_getduration_ms(p) / 2);

		bool not_even_close = (tdiff.time.tv_sec == 0 && tdiff.time.tv_nsec < not_even_close_ms * 1000000L);
		if (!not_even_close) {
			log_error("Timing is not even close, ");
			if (tdiff.sign < 0) {
				log_error("we are ahead by %s seconds\n", print_timespec(&tdiff.time));
				if (!chunk_is_empty(p)) {
					memset(p->data, 0, p->size);
					p->play_at_tv_sec = 0;
					log_error("replacing data with silence\n");
				}

				snapctx.alsaplayer_ctx.playing = false;
				snapctx.alsaplayer_ctx.empty_chunks_in_row = 0;
			} else {
				if (p->play_at_tv_sec > 0) {
					log_error("we are behind by %s seconds: dropping this chunk!\n", print_timespec(&tdiff.time));
					p->size = 0;
					p->play_at_tv_sec = 0;
					chunk_free_members(p);
					return -1;
				} else {
					log_error(" playing empty chunk\n");
				}
			}
		}
	}

	if (!chunk_is_empty(p))  // Do not stretch, when chunk contains only silence, save some CPU
		adjust_speed(p, factor);

	log_verbose("status: %d factor: %f chunk: chunksize: %d current time: %s, play_at: %s difference: %s sign: %d empty %d\n",
		    snapctx.alsaplayer_ctx.playing, factor, p->size, print_timespec(&ctime), print_timespec(&ts_alsa_ready),
		    print_timespec(&tdiff.time), tdiff.sign, chunk_is_empty(p));

	return p->size;
}

void alsaplayer_handle(alsaplayer_ctx *ctx) {
	unsigned int pcm;
	snd_pcm_sframes_t delayp = 0;
	pcmChunk chunk;
	// initialize chunk. This will only be used if empty chunks are generated because of timing issues
	chunk.samples = ctx->rate;
	chunk.channels = ctx->channels;
	chunk.frame_size = ctx->frame_size;

	if (snd_pcm_delay(ctx->pcm_handle, &delayp) < 0)
		log_verbose("could not obtain pcm delay\n");

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

	if (!(chunk.channels && chunk.frame_size && chunk.size)) {
		log_error("retrieved zero chunk, not writing to alsa device\n");
		return;
	}

	if ((pcm = snd_pcm_writei(ctx->pcm_handle, chunk.data, chunk.size / chunk.channels / chunk.frame_size)) == -EPIPE) {
		log_error("Alsa buffer drained. This will be audible.\n");
		snd_pcm_prepare(ctx->pcm_handle);
	} else if (pcm < 0) {
		log_error("ERROR. Can't write to PCM device. %s, snd_pcm_recover(%d)\n", snd_strerror(pcm),
			  (int)snd_pcm_recover(ctx->pcm_handle, pcm, 0));
	} else if (pcm < chunk.size / chunk.channels / chunk.frame_size) {
		log_debug("delay frames (split): %d\n", delayp);
		if (!ctx->overflow) {
			ctx->overflow = snap_alloc(sizeof(pcmChunk));
			chunk_copy_meta(ctx->overflow, &chunk);
			ctx->overflow->data = chunk.data;
		}
		pcmchunk_shaveoff(ctx->overflow, pcm);

		log_debug("Wrote %d/%d frames to pcm - splitting chunk to write the rest later\n", pcm,
			  chunk.size / chunk.channels / chunk.frame_size);
	} else if (pcm == chunk.size / chunk.channels / chunk.frame_size) {
		log_debug("delay frames: %d\n", delayp);
		if (chunk.size) {
			chunk_free_members(&chunk);
		}
		free(ctx->overflow);
		ctx->overflow = NULL;
	}
}

void alsaplayer_uninit_task(void *d) {
	alsaplayer_ctx *ctx = (alsaplayer_ctx *)d;
	log_verbose("unititializing alsa after timeout\n");
	alsaplayer_uninit(ctx);
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

	if (ctx->close_task)
		taskqueue_remove(ctx->close_task);
	ctx->close_task = NULL;

	if (ctx->main_poll_fd)
		for (int i = 0; i < ctx->pollfd_count; ++i) {
			log_verbose("uninitializing alsa fd %d on index %d\n", ctx->main_poll_fd[i].fd, i);
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

void mixer_init(alsaplayer_ctx *ctx) {
	snd_mixer_selem_id_t *sid = NULL;
	snd_mixer_open(&ctx->mixer_handle, 0);  // mode is unused => setting 0
	snd_mixer_attach(ctx->mixer_handle, ctx->card);
	snd_mixer_selem_register(ctx->mixer_handle, NULL, NULL);
	snd_mixer_load(ctx->mixer_handle);

	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, ctx->mixer);
	ctx->mixer_elem = snd_mixer_find_selem(ctx->mixer_handle, sid);

	snd_mixer_selem_get_playback_volume_range(ctx->mixer_elem, &ctx->mixer_min, &ctx->mixer_max);
}

void mixer_uninit(alsaplayer_ctx *ctx) {
	snd_mixer_close(ctx->mixer_handle);
	ctx->mixer_handle = NULL;
}

uint8_t obtain_volume(alsaplayer_ctx *ctx) {
	long volume = 0;
	mixer_init(ctx);
	snd_mixer_selem_get_playback_volume(ctx->mixer_elem, SND_MIXER_SCHN_MONO, &volume);
	mixer_uninit(ctx);
	uint8_t ret = (uint8_t)(round((double)volume * 100 / ctx->mixer_max));
	log_debug("Obtained volume (raw): %d\n", ret);
	return ret;
}

void adjustVolume(alsaplayer_ctx *ctx, uint8_t volume) {
	mixer_init(ctx);
	snd_mixer_selem_set_playback_volume_all(ctx->mixer_elem, volume * ctx->mixer_max / 100);
	mixer_uninit(ctx);
}

void alsaplayer_init(alsaplayer_ctx *ctx) {
	unsigned int pcm, tmp;
	int err;

	ctx->empty_chunks_in_row = 0;
	ctx->playing = false;
	ctx->overflow = NULL;
	ctx->mixer_handle = NULL;

	if (ctx->initialized)
		return;

	log_verbose("initializing alsa\n");
	ctx->close_task = post_task(&snapctx.taskqueue_ctx, (snapctx.bufferms * 1.2) / 1000, (int)(snapctx.bufferms * 1.2) % 1000,
				    alsaplayer_uninit_task, NULL, ctx);

	int buff_size;

	ctx->pcm_handle = NULL;
	if ((pcm = snd_pcm_open(&ctx->pcm_handle, ctx->pcm.name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0)
		log_error("ERROR: Cannot open \"%s\" PCM device. %s\n", ctx->pcm.name, snd_strerror(pcm));

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
		exit_error("unsupported format: frame_size %lu\n", ctx->frame_size);

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

	unsigned int buffer_time = 20 * tmp;
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

	if ((err = snd_pcm_poll_descriptors(ctx->pcm_handle, ctx->ufds, ctx->pollfd_count)) < 0)
		exit_error("Unable to obtain poll descriptors for playback: %s\n", snd_strerror(err));

	snd_pcm_sw_params_alloca(&ctx->swparams);

	snd_pcm_sw_params_current(ctx->pcm_handle, ctx->swparams);

	snd_pcm_sw_params_set_avail_min(ctx->pcm_handle, ctx->swparams, ctx->frames);

	snd_pcm_sw_params_set_start_threshold(ctx->pcm_handle, ctx->swparams, ctx->frames);

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
