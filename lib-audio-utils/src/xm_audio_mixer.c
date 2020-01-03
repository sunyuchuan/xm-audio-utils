#include "xm_audio_mixer.h"
#include "pcm_parser.h"
#include "json/json_parse.h"
#include <pthread.h>
#include "voice_mixer_struct.h"
#include "mixer_effects/side_chain_compress.h"
#include "error_def.h"
#include "log.h"
#include "tools/util.h"
#include "tools/fifo.h"
#include "tools/mem.h"
#include <stdlib.h>
#include <string.h>
#include "tools/conversion.h"

#define DEFAULT_SAMPLE_RATE 44100
#define DEFAULT_CHANNEL_NUMBER 2

enum MiddleBuffersType {
    VoicePcm = 0,
    Decoder,
    MixBgm,
    MixMusic,
    NB_MIDDLE_BUFFERS
};

struct XmMixerContext_T {
    volatile bool abort;
    int mix_status;
    int progress;
    // input pcm sample rate and number channels
    int pcm_sample_rate;
    int pcm_channels;
    int dst_sample_rate;
    int dst_channels;
    // input pcm file seek position
    int seek_time_ms;
    // input pcm read location
    int64_t cur_size;
    // input pcm file parser
    PcmParser *parser;
    short *middle_buffer[NB_MIDDLE_BUFFERS];
    fifo *audio_fifo;
    char *in_config_path;
    pthread_mutex_t mutex;
    MixerEffcets mixer_effects;
};

static void mixer_effects_free(MixerEffcets *mixer) {
    LogInfo("%s\n", __func__);
    if (NULL == mixer)
        return;

    if (mixer->bgm) {
        audio_source_freep(&mixer->bgm);
    }
    if (mixer->bgmQueue) {
        source_queue_freep(&mixer->bgmQueue);
    }
    if (mixer->music) {
        audio_source_freep(&mixer->music);
    }
    if (mixer->musicQueue) {
        source_queue_freep(&mixer->musicQueue);
    }
}

static int mixer_effects_init(MixerEffcets *mixer) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if (NULL == mixer)
        return ret;

    mixer_effects_free(mixer);

    mixer->bgm = (AudioSource *)calloc(1, sizeof(AudioSource));
    if (NULL == mixer->bgm) {
        LogError("%s alloc AudioSource failed.\n", __func__);
        ret = -1;
        goto fail;
    }
    mixer->bgmQueue = source_queue_create();

    mixer->music = (AudioSource *)calloc(1, sizeof(AudioSource));
    if (NULL == mixer->music) {
        LogError("%s alloc AudioSource failed.\n", __func__);
        ret = -1;
        goto fail;
    }
    mixer->musicQueue = source_queue_create();

    ret = 0;
fail:
    return ret;
}

static PcmParser *open_source_parser(AudioSource *source,
        int dst_sample_rate, int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!source || !source->file_path)
        return NULL;

    if (source->parser) {
        pcm_parser_freep(&(source->parser));
    }

    if ((source->parser = pcm_parser_create(source->file_path,
            source->sample_rate, source->nb_channels,
            dst_sample_rate, dst_channels)) == NULL) {
	LogError("%s open source pcm parser failed, file addr %s.\n", __func__, source->file_path);
	return NULL;
    }

    source->fade_io.fade_in_nb_samples = source->fade_io.fade_in_time_ms * dst_sample_rate / 1000;
    source->fade_io.fade_out_nb_samples = source->fade_io.fade_out_time_ms * dst_sample_rate / 1000;
    source->yl_prev = source->makeup_gain * MAKEUP_GAIN_MAX_DB;
    pcm_parser_seekTo(source->parser, seek_time_ms);
    return source->parser;
}

static int update_audio_source(AudioSourceQueue *queue,
        AudioSource *source, int dst_sample_rate, int dst_channels) {
    int ret = -1;
    if (!queue || !source)
        return ret;

    if (source_queue_size(queue) > 0) {
        audio_source_free(source);
        if (source_queue_get(queue, source) > 0) {
            PcmParser *parser = open_source_parser(source, dst_sample_rate, dst_channels, 0);
            if (!parser)
            {
                LogError("%s open pcm parser failed, file_path: %s.\n", __func__, source->file_path);
                ret = AEERROR_NOMEM;
            } else {
                ret = 0;
            }
        }
    }
    return ret;
}

static void audio_source_seekTo(AudioSourceQueue *queue,
        AudioSource *source, int dst_sample_rate, int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!queue || !source || (source_queue_size(queue) <= 0))
        return;

    int bgm_seek_time = 0;
    bool find_source = false;
    while ((source_queue_size(queue) > 0)) {
        audio_source_free(source);
        if (source_queue_get(queue, source) > 0) {
            if (source->start_time_ms <= seek_time_ms) {
                if (source->end_time_ms <= seek_time_ms) {
                    bgm_seek_time = 0;
                } else {
                    bgm_seek_time = seek_time_ms - source->start_time_ms;
                    find_source = true;
                    break;
                }
            } else {
                bgm_seek_time = 0;
                find_source = true;
                break;
            }
        }
    }
    if (find_source)
        open_source_parser(source, dst_sample_rate, dst_channels, bgm_seek_time);
    else
        audio_source_free(source);
}

static void fade_in_out(AudioSource *source, int sample_rate,
        int channels, int pcm_start_time, int pcm_duration,
        short *dst_buffer, int dst_buffer_size) {
    if (!source || !dst_buffer)
        return;

    check_fade_in_out(&(source->fade_io), pcm_start_time, pcm_duration,
        sample_rate, source->start_time_ms, source->end_time_ms);
    scale_with_ramp(&(source->fade_io), dst_buffer,
        dst_buffer_size / channels, channels, source->volume);
}

static int mixer_chk_st_l(int mix_state)
{
    if (mix_state == MIX_STATE_INITIALIZED) {
        return 0;
    }

    LogError("%s mixer state(%d) is invalid.\n", __func__, mix_state);
    LogError("%s expecting mix_state == MIX_STATE_INITIALIZED(1).\n", __func__);
    return -1;
}

static void mixer_abort_l(XmMixerContext *ctx)
{
    if(!ctx)
        return;

    pthread_mutex_lock(&ctx->mutex);
    ctx->abort= true;
    pthread_mutex_unlock(&ctx->mutex);
}

static void mixer_free_l(XmMixerContext *ctx)
{
    if(!ctx)
        return;
    mixer_abort_l(ctx);

    mixer_effects_free(&(ctx->mixer_effects));

    if (ctx->in_config_path) {
        free(ctx->in_config_path);
        ctx->in_config_path = NULL;
    }

    if (ctx->audio_fifo) {
        fifo_delete(&ctx->audio_fifo);
    }

    for (int i = 0; i < NB_MIDDLE_BUFFERS; i++) {
        if (ctx->middle_buffer[i]) {
            free(ctx->middle_buffer[i]);
            ctx->middle_buffer[i] = NULL;
        }
    }

    if (ctx->parser) {
        pcm_parser_freep(&ctx->parser);
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->abort= false;
    ctx->progress = 0;
    pthread_mutex_unlock(&ctx->mutex);
}

static short *mixer_mix(XmMixerContext *ctx, short *pcm_buffer,
        int pcm_buffer_size, int pcm_start_time, int pcm_duration,
        AudioSource *source, short *decoder_buffer, short *dst_buffer) {
    if (!ctx || !pcm_buffer || !source
            || !decoder_buffer || !dst_buffer)
        return NULL;

    short *mix_buffer = NULL;
    int dst_sample_rate = ctx->dst_sample_rate;
    int dst_channels = ctx->dst_channels;
    PcmParser *parser = source->parser;
    if (!parser) {
        return pcm_buffer;
    }

    if (pcm_start_time >= source->start_time_ms &&
            pcm_start_time + pcm_duration < source->end_time_ms) {
        int decoder_buffer_size = pcm_parser_get_pcm_frame(parser,
                decoder_buffer, pcm_buffer_size, true);
        if (decoder_buffer_size <= 0) {
            LogWarning("%s 1 pcm_parser_get_pcm_frame size is zero.\n", __func__);
            mix_buffer = pcm_buffer;
            goto end;
        }

        fade_in_out(source, dst_sample_rate, dst_channels,
                pcm_start_time, pcm_duration, decoder_buffer, decoder_buffer_size);
        if (source->side_chain_enable) {
            side_chain_compress(pcm_buffer, decoder_buffer, &(source->yl_prev),
                    decoder_buffer_size, dst_sample_rate, dst_channels,
                    SIDE_CHAIN_THRESHOLD, SIDE_CHAIN_RATIO, SIDE_CHAIN_ATTACK_MS,
                    SIDE_CHAIN_RELEASE_MS, source->makeup_gain);
        }
        MixBufferS16(pcm_buffer, decoder_buffer, decoder_buffer_size / dst_channels,
                dst_channels, dst_buffer, &(source->left_factor), &(source->right_factor));

        if (decoder_buffer_size < pcm_buffer_size) {
            memcpy(dst_buffer + decoder_buffer_size, pcm_buffer + decoder_buffer_size,
                sizeof(short) * (pcm_buffer_size - decoder_buffer_size));
        }
        mix_buffer = dst_buffer;
    } else if (pcm_start_time < source->start_time_ms &&
            pcm_start_time + pcm_duration > source->start_time_ms) {
        int decoder_start_index = ((source->start_time_ms - pcm_start_time) / (float)1000)
            * dst_sample_rate * dst_channels;
        memcpy(dst_buffer, pcm_buffer, sizeof(short) * decoder_start_index);

        int decoder_buffer_size = pcm_parser_get_pcm_frame(parser,
            decoder_buffer, pcm_buffer_size - decoder_start_index, true);
        if (decoder_buffer_size <= 0) {
            LogWarning("%s 2 pcm_parser_get_pcm_frame size is zero.\n", __func__);
            mix_buffer = pcm_buffer;
            goto end;
        }

        fade_in_out(source, dst_sample_rate, dst_channels,
                pcm_start_time, pcm_duration, decoder_buffer, decoder_buffer_size);
        if (source->side_chain_enable) {
            side_chain_compress(pcm_buffer + decoder_start_index, decoder_buffer, &(source->yl_prev),
                    decoder_buffer_size, dst_sample_rate, dst_channels,
                    SIDE_CHAIN_THRESHOLD, SIDE_CHAIN_RATIO, SIDE_CHAIN_ATTACK_MS,
                    SIDE_CHAIN_RELEASE_MS, source->makeup_gain);
        }
        MixBufferS16(pcm_buffer + decoder_start_index, decoder_buffer,
            decoder_buffer_size / dst_channels, dst_channels,
            dst_buffer + decoder_start_index,
            &(source->left_factor), &(source->right_factor));

        if (decoder_buffer_size < (pcm_buffer_size - decoder_start_index)) {
            memcpy(dst_buffer + decoder_start_index + decoder_buffer_size,
                pcm_buffer + decoder_start_index + decoder_buffer_size, sizeof(short)
                * (pcm_buffer_size - decoder_start_index - decoder_buffer_size));
        }
        mix_buffer = dst_buffer;
    } else if (pcm_start_time <= source->end_time_ms &&
            pcm_start_time + pcm_duration > source->end_time_ms) {
        int decoder_size_in_short = ((source->end_time_ms - pcm_start_time) / (float)1000)
            * dst_sample_rate * dst_channels;
        int decoder_buffer_size = pcm_parser_get_pcm_frame(parser,
            decoder_buffer, decoder_size_in_short, true);
        if (decoder_buffer_size <= 0) {
            LogWarning("%s 3 pcm_parser_get_pcm_frame size is zero, decoder_size_in_short is %d.\n", __func__, decoder_size_in_short);
            mix_buffer = pcm_buffer;
            //update the decoder that point the next bgm
            audio_source_free(source);
            goto end;
        }

        fade_in_out(source, dst_sample_rate, dst_channels,
                pcm_start_time, pcm_duration, decoder_buffer, decoder_buffer_size);
        if (source->side_chain_enable) {
            side_chain_compress(pcm_buffer, decoder_buffer, &(source->yl_prev),
                    decoder_buffer_size, dst_sample_rate, dst_channels,
                    SIDE_CHAIN_THRESHOLD, SIDE_CHAIN_RATIO, SIDE_CHAIN_ATTACK_MS,
                    SIDE_CHAIN_RELEASE_MS, source->makeup_gain);
        }
        MixBufferS16(pcm_buffer, decoder_buffer, decoder_buffer_size / dst_channels,
            dst_channels, dst_buffer, &(source->left_factor), &(source->right_factor));

        memcpy(dst_buffer + decoder_buffer_size, pcm_buffer + decoder_buffer_size,
            sizeof(short) * (pcm_buffer_size - decoder_buffer_size));
        mix_buffer = dst_buffer;
        //update the decoder that point the next bgm
        audio_source_free(source);
    } else {
        mix_buffer = pcm_buffer;
    }

end:
    return mix_buffer;
}

static int mixer_mix_and_write_fifo(XmMixerContext *ctx) {
    int ret = -1;
    if (!ctx || !ctx->parser)
        return ret;

    int buffer_start_ms = ctx->seek_time_ms +
        1000 * ((float)ctx->cur_size / ctx->dst_channels / ctx->dst_sample_rate);
    int read_len = pcm_parser_get_pcm_frame(ctx->parser,
        ctx->middle_buffer[VoicePcm], MAX_NB_SAMPLES, false);
    if (read_len <= 0) {
        ret = read_len;
        goto end;
    }
    int duration =
        1000 * ((float)read_len / ctx->dst_channels / ctx->dst_sample_rate);
    ctx->cur_size += read_len;

    if (!ctx->mixer_effects.bgm->parser) {
        update_audio_source(ctx->mixer_effects.bgmQueue,
            ctx->mixer_effects.bgm, ctx->dst_sample_rate, ctx->dst_channels);
    }
    short *voice_bgm_buffer = mixer_mix(ctx, ctx->middle_buffer[VoicePcm],
        read_len, buffer_start_ms, duration, ctx->mixer_effects.bgm,
        ctx->middle_buffer[Decoder], ctx->middle_buffer[MixBgm]);
    if (voice_bgm_buffer == NULL) {
        LogError("mixing voice and bgm failed.\n");
        goto end;
    }

    if (!ctx->mixer_effects.music->parser) {
        update_audio_source(ctx->mixer_effects.musicQueue,
            ctx->mixer_effects.music, ctx->dst_sample_rate, ctx->dst_channels);
    }
    short *voice_bgm_music_buffer = mixer_mix(ctx, voice_bgm_buffer,
        read_len, buffer_start_ms, duration, ctx->mixer_effects.music,
        ctx->middle_buffer[Decoder], ctx->middle_buffer[MixMusic]);
    if (voice_bgm_music_buffer == NULL) {
        LogError("mixing voice_bgm and music failed.\n");
        goto end;
    }

    ret = fifo_write(ctx->audio_fifo, voice_bgm_music_buffer, read_len);
    if (ret < 0) goto end;
    ret = read_len;

end:
    return ret;
}

void xm_audio_mixer_freep(XmMixerContext **ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx || NULL == *ctx)
        return;
    XmMixerContext *self = *ctx;

    mixer_free_l(self);
    pthread_mutex_destroy(&(self->mutex));
    free(*ctx);
    *ctx = NULL;
}

void xm_audio_mixer_stop(XmMixerContext *ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx)
        return;

    mixer_abort_l(ctx);
}

int xm_audio_mixer_get_progress(XmMixerContext *ctx) {
    if (NULL == ctx)
        return 0;

    int ret = 0;
    pthread_mutex_lock(&ctx->mutex);
    ret = ctx->progress;
    pthread_mutex_unlock(&ctx->mutex);

    return ret;
}

int xm_audio_mixer_get_frame(XmMixerContext *ctx,
    short *buffer, int buffer_size_in_short) {
    int ret = -1;
    if (!ctx || !buffer || buffer_size_in_short < 0)
        return ret;

    while (fifo_occupancy(ctx->audio_fifo) < (size_t) buffer_size_in_short) {
	ret = mixer_mix_and_write_fifo(ctx);
	if (ret < 0) {
	    if (0 < fifo_occupancy(ctx->audio_fifo)) {
	        break;
	    } else {
	        goto end;
	    }
	}
    }

    return fifo_read(ctx->audio_fifo, buffer, buffer_size_in_short);
end:
    return ret;
}

int xm_audio_mixer_seekTo(XmMixerContext *ctx,
        int seek_time_ms) {
    LogInfo("%s seek_time_ms %d.\n", __func__, seek_time_ms);
    if (!ctx || !ctx->parser)
        return -1;

    ctx->seek_time_ms = seek_time_ms > 0 ? seek_time_ms : 0;
    if (ctx->audio_fifo) fifo_clear(ctx->audio_fifo);

    int ret = pcm_parser_seekTo(ctx->parser, ctx->seek_time_ms);
    ctx->cur_size = 0;

    if ((ret = mixer_effects_init(&(ctx->mixer_effects))) < 0) {
        LogError("%s mixer_effects_init failed\n", __func__);
        return ret;
    }

    if ((ret = mixer_parse(&(ctx->mixer_effects), ctx->in_config_path)) < 0) {
        LogError("%s mixer_parse %s failed\n", __func__, ctx->in_config_path);
        return ret;
    }

    audio_source_seekTo(ctx->mixer_effects.bgmQueue, ctx->mixer_effects.bgm,
        ctx->dst_sample_rate, ctx->dst_channels, ctx->seek_time_ms);
    audio_source_seekTo(ctx->mixer_effects.musicQueue, ctx->mixer_effects.music,
        ctx->dst_sample_rate, ctx->dst_channels, ctx->seek_time_ms);
    return ret;
}

/**
 * In order to avoid resampling the pcm data,
 * the target sampling rate should be the same as
 *         the sampling rate of voice pcm.
 */
static int xm_audio_mixer_mix_l(XmMixerContext *ctx,
    const char *out_file_path) {
    LogInfo("%s.\n", __func__);
    int ret = -1;
    short *buffer = NULL;
    if (!ctx || !out_file_path || !ctx->parser) {
        return ret;
    }

    FILE *writer = NULL;
    if ((ret = ae_open_file(&writer, out_file_path, true)) < 0) {
	LogError("%s open output pcm file %s failed\n", __func__, out_file_path);
	return ret;
    }

    buffer = (short *)calloc(sizeof(short), MAX_NB_SAMPLES);
    if (!buffer) {
        LogError("%s calloc buffer failed.\n", __func__);
        ret = AEERROR_NOMEM;
        goto fail;
    }

    ctx->seek_time_ms = 0;
    ctx->cur_size = 0;
    PcmParser *parser = ctx->parser;
    float file_duration = parser->file_size / 2 / ctx->pcm_channels /
        ctx->pcm_sample_rate;
    while (!ctx->abort) {
        float cur_position = ctx->cur_size / ctx->dst_channels /
            ctx->dst_sample_rate;
        int progress = (cur_position / file_duration) * 100;
        pthread_mutex_lock(&ctx->mutex);
        ctx->progress = progress;
        pthread_mutex_unlock(&ctx->mutex);

        ret = xm_audio_mixer_get_frame(ctx, buffer, MAX_NB_SAMPLES);
        if (ret <= 0) {
            LogInfo("xm_audio_mixer_get_frame len <= 0.\n");
            break;
        }

        fwrite(buffer, sizeof(short), ret, writer);
    }

fail:
    if (buffer != NULL) {
        free(buffer);
        buffer = NULL;
    }
    if (writer) {
        fclose(writer);
        writer = NULL;
    }
    return 0;
}

int xm_audio_mixer_mix(XmMixerContext *ctx,
    const char *out_file_path)
{
    LogInfo("%s out_file_path = %s.\n", __func__, out_file_path);
    int ret = -1;
    if (NULL == ctx || NULL == out_file_path) {
        return ret;
    }

    if (mixer_chk_st_l(ctx->mix_status) < 0) {
        return AEERROR_INVALID_STATE;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->mix_status = MIX_STATE_STARTED;
    pthread_mutex_unlock(&ctx->mutex);

    if ((ret = xm_audio_mixer_mix_l(ctx, out_file_path)) < 0) {
        LogError("%s mixer_audio_mix_l failed\n", __func__);
        goto fail;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->mix_status = MIX_STATE_COMPLETED;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
fail:
    mixer_free_l(ctx);
    pthread_mutex_lock(&ctx->mutex);
    ctx->mix_status = MIX_STATE_ERROR;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
}

int xm_audio_mixer_init(XmMixerContext *ctx,
        const char *in_pcm_path, int pcm_sample_rate, int pcm_channels,
        int dst_sample_rate, int dst_channels, const char *in_config_path)
{
    int ret = -1;
    if (!ctx || !in_config_path || !in_pcm_path) {
        return ret;
    }
    if (pcm_sample_rate != dst_sample_rate) {
        LogError("%s unsupport pcm resampling.\n", __func__);
        return ret;
    }
    LogInfo("%s in_pcm_path = %s in_config_path = %s.\n", __func__, in_pcm_path, in_config_path);

    mixer_free_l(ctx);
    ctx->pcm_sample_rate = pcm_sample_rate;
    ctx->pcm_channels = pcm_channels;
    ctx->dst_sample_rate = dst_sample_rate;
    ctx->dst_channels = dst_channels;
    ctx->cur_size = 0;
    ctx->seek_time_ms = 0;
    ctx->in_config_path = av_strdup(in_config_path);

    if ((ret = mixer_effects_init(&(ctx->mixer_effects))) < 0) {
        LogError("%s mixer_effects_init failed\n", __func__);
        goto fail;
    }

    if ((ret = mixer_parse(&(ctx->mixer_effects), in_config_path)) < 0) {
        LogError("%s mixer_parse %s failed\n", __func__, in_config_path);
        goto fail;
    }

    if ((ctx->parser = pcm_parser_create(in_pcm_path, pcm_sample_rate,
	    pcm_channels, dst_sample_rate, dst_channels)) == NULL) {
	LogError("%s pcm_parser_create failed, file addr : %s.\n", __func__, in_pcm_path);
	goto fail;
    }

    for (int i = 0; i < NB_MIDDLE_BUFFERS; i++) {
        ctx->middle_buffer[i] = (short *)calloc(sizeof(short), MAX_NB_SAMPLES);
        if (!ctx->middle_buffer[i]) {
            LogError("%s calloc middle_buffer[%d] failed.\n", __func__, i);
            ret = AEERROR_NOMEM;
            goto fail;
        }
    }

    // Allocate buffer for audio fifo
    ctx->audio_fifo = fifo_create(sizeof(int16_t));
    if (!ctx->audio_fifo) {
        LogError("%s Could not allocate audio FIFO\n", __func__);
        ret = AEERROR_NOMEM;
        goto fail;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->mix_status = MIX_STATE_INITIALIZED;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
fail:
    mixer_free_l(ctx);
    pthread_mutex_lock(&ctx->mutex);
    ctx->mix_status = MIX_STATE_ERROR;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
}

XmMixerContext *xm_audio_mixer_create()
{
    LogInfo("%s.\n", __func__);
    XmMixerContext *self = (XmMixerContext *)calloc(1, sizeof(XmMixerContext));
    if (NULL == self) {
        LogError("%s alloc XmMixerContext failed.\n", __func__);
        return NULL;
    }

    self->dst_sample_rate = DEFAULT_SAMPLE_RATE;
    self->dst_channels = DEFAULT_CHANNEL_NUMBER;
    pthread_mutex_init(&self->mutex, NULL);
    self->mix_status = MIX_STATE_UNINIT;

    return self;
}
