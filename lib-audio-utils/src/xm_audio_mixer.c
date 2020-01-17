#include "xm_audio_mixer.h"
#include "json/json_parse.h"
#include "codec/audio_muxer.h"
#include <pthread.h>
#include "voice_mixer_struct.h"
#include "mixer_effects/side_chain_compress.h"
#include "error_def.h"
#include "log.h"
#include "tools/util.h"
#include "tools/fifo.h"
#include "pcm_parser.h"
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
    AudioMuxer *muxer ;
    char *in_config_path;
    pthread_mutex_t mutex;
    MixerEffcets mixer_effects;
};

static void mixer_effects_free(MixerEffcets *mixer) {
    LogInfo("%s\n", __func__);
    if (NULL == mixer)
        return;

    if (mixer->record) {
        audio_record_source_freep(&mixer->record);
    }
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

    mixer->record = (AudioRecordSource *)calloc(1, sizeof(AudioRecordSource));
    if (NULL == mixer->record) {
        LogError("%s alloc AudioRecordSource failed.\n", __func__);
        ret = -1;
        goto fail;
    }

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

static AudioDecoder *open_source_decoder(AudioSource *source,
        int dst_sample_rate, int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!source || !source->file_path)
        return NULL;
    AudioDecoder *decoder = NULL;

    decoder = xm_audio_decoder_create(source->file_path,
        dst_sample_rate, dst_channels);
    if (!decoder)
    {
        LogError("%s malloc bgm_music decoder failed.\n", __func__);
        return NULL;
    }
    source->fade_io.fade_in_nb_samples = source->fade_io.fade_in_time_ms * dst_sample_rate / 1000;
    source->fade_io.fade_out_nb_samples = source->fade_io.fade_out_time_ms * dst_sample_rate / 1000;
    source->yl_prev = source->makeup_gain * MAKEUP_GAIN_MAX_DB;
    xm_audio_decoder_seekTo(decoder, seek_time_ms);
    source->decoder = decoder;
    return decoder;
}

static int update_audio_source(AudioSourceQueue *queue,
        AudioSource *source, int dst_sample_rate, int dst_channels) {
    int ret = -1;
    if (!queue || !source)
        return ret;

    while (source_queue_size(queue) > 0) {
        audio_source_free(source);
        if (source_queue_get(queue, source) > 0) {
            source->decoder = open_source_decoder(source, dst_sample_rate, dst_channels, 0);
            if (!source->decoder)
            {
                LogError("%s open decoder failed, file_path: %s.\n", __func__, source->file_path);
                ret = AEERROR_NOMEM;
            } else {
                ret = 0;
                break;
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
        open_source_decoder(source, dst_sample_rate, dst_channels, bgm_seek_time);
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

static AudioMuxer *open_muxer(int dst_sample_rate, int dst_channels,
	const char *out_file_path, int encoder_type) {
    LogInfo("%s\n", __func__);
    if (!out_file_path )
        return NULL;
    AudioMuxer *muxer = NULL;

    MuxerConfig config;
    config.src_sample_rate_in_Hz = dst_sample_rate;
    config.src_nb_channels = dst_channels;
    config.dst_sample_rate_in_Hz = dst_sample_rate;
    config.dst_nb_channels = dst_channels;
    config.mime = MIME_AUDIO_AAC;
    config.output_filename = av_strdup(out_file_path);
    config.src_sample_fmt = AV_SAMPLE_FMT_S16;
    config.codec_id = AV_CODEC_ID_AAC;
    switch (encoder_type) {
        case ENCODER_FFMPEG:
            config.encoder_type = ENCODER_FFMPEG;
            break;
        case ENCODER_MEDIA_CODEC:
            config.encoder_type = ENCODER_MEDIA_CODEC;
            break;
        default:
            LogError("%s encoder_type %d is invalid.\n", __func__, encoder_type);
            config.encoder_type = ENCODER_NONE;
            return NULL;
    }
    muxer = muxer_create(&config);
    free(config.output_filename);
    return muxer;
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

    xm_audio_mixer_stop(ctx);

    mixer_effects_free(&(ctx->mixer_effects));

    if (ctx->in_config_path) {
        free(ctx->in_config_path);
        ctx->in_config_path = NULL;
    }

    muxer_stop(ctx->muxer);
    muxer_freep(&(ctx->muxer));

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

static short *mixer_combine(XmMixerContext *ctx, short *pcm_buffer,
        int pcm_buffer_size, int pcm_start_time, int pcm_duration,
        AudioSource *source, short *decoder_buffer, short *dst_buffer) {
    if (!ctx || !pcm_buffer || !source
            || !decoder_buffer || !dst_buffer)
        return NULL;

    short *mix_buffer = NULL;
    int dst_sample_rate = ctx->dst_sample_rate;
    int dst_channels = ctx->dst_channels;
    AudioDecoder *decoder = source->decoder;
    if (!decoder) {
        return pcm_buffer;
    }

    int decode_size_in_short = 0;
    int decode_data_start_index = 0;
    memset(decoder_buffer, 0, sizeof(short) * MAX_NB_SAMPLES);
    if (pcm_start_time >= source->start_time_ms &&
            pcm_start_time + pcm_duration < source->end_time_ms) {
        decode_data_start_index = 0;

        int decoder_size_in_short = pcm_buffer_size > 0 ? pcm_buffer_size : 0;
        decode_size_in_short = xm_audio_decoder_get_decoded_frame(decoder,
                decoder_buffer, decoder_size_in_short, true);
        if (decode_size_in_short <= 0) {
            LogWarning("%s 1 decoder_get_decoded_frame size is zero.\n", __func__);
            mix_buffer = pcm_buffer;
            goto end;
        }
    } else if (pcm_start_time < source->start_time_ms &&
            pcm_start_time + pcm_duration > source->start_time_ms) {
        decode_data_start_index = ((source->start_time_ms - pcm_start_time) / (float)1000)
            * dst_sample_rate * dst_channels;
        int decoder_size_in_short = pcm_buffer_size -
            decode_data_start_index > 0 ? pcm_buffer_size - decode_data_start_index : 0;

        decode_size_in_short = xm_audio_decoder_get_decoded_frame(decoder,
            decoder_buffer + decode_data_start_index, decoder_size_in_short, true);
        if (decode_size_in_short <= 0) {
            LogWarning("%s 2 decoder_get_decoded_frame size is zero.\n", __func__);
            mix_buffer = pcm_buffer;
            goto end;
        }
    } else if (pcm_start_time <= source->end_time_ms &&
            pcm_start_time + pcm_duration > source->end_time_ms) {
        decode_data_start_index = 0;
        int decoder_size_in_short = ((source->end_time_ms - pcm_start_time) / (float)1000)
            * dst_sample_rate * dst_channels;
        decoder_size_in_short = decoder_size_in_short > 0 ? decoder_size_in_short : 0;

        decode_size_in_short = xm_audio_decoder_get_decoded_frame(decoder,
            decoder_buffer, decoder_size_in_short, true);
        //update the decoder that point the next bgm
        audio_source_free(source);
        if (decode_size_in_short <= 0) {
            LogWarning("%s 3 decoder_get_decoded_frame size is zero, decoder_size_in_short is %d.\n", __func__, decoder_size_in_short);
            mix_buffer = pcm_buffer;
            goto end;
        }
    } else {
        mix_buffer = pcm_buffer;
        goto end;
    }

    fade_in_out(source, dst_sample_rate, dst_channels, pcm_start_time,
        pcm_duration, decoder_buffer + decode_data_start_index, decode_size_in_short);
    if (source->side_chain_enable) {
        side_chain_compress(pcm_buffer + decode_data_start_index,
            decoder_buffer + decode_data_start_index, &(source->yl_prev),
            decode_size_in_short, dst_sample_rate, dst_channels,
            SIDE_CHAIN_THRESHOLD, SIDE_CHAIN_RATIO, SIDE_CHAIN_ATTACK_MS,
            SIDE_CHAIN_RELEASE_MS, source->makeup_gain);
    }
    MixBufferS16(pcm_buffer, decoder_buffer, pcm_buffer_size / dst_channels,
        dst_channels, dst_buffer, &(source->left_factor), &(source->right_factor));

    mix_buffer = dst_buffer;
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
    if (read_len < 0) {
        ret = read_len;
        goto end;
    }
    int duration =
        1000 * ((float)read_len / ctx->dst_channels / ctx->dst_sample_rate);
    ctx->cur_size += read_len;

    if ((!ctx->mixer_effects.bgm->decoder) &&
            (source_queue_size(ctx->mixer_effects.bgmQueue) > 0)) {
        update_audio_source(ctx->mixer_effects.bgmQueue,
            ctx->mixer_effects.bgm, ctx->dst_sample_rate, ctx->dst_channels);
    }
    short *voice_bgm_buffer = mixer_combine(ctx, ctx->middle_buffer[VoicePcm],
        read_len, buffer_start_ms, duration, ctx->mixer_effects.bgm,
        ctx->middle_buffer[Decoder], ctx->middle_buffer[MixBgm]);
    if (voice_bgm_buffer == NULL) {
        LogError("mixing voice and bgm failed.\n");
        goto end;
    }

    if ((!ctx->mixer_effects.music->decoder) &&
            (source_queue_size(ctx->mixer_effects.musicQueue) > 0)) {
        update_audio_source(ctx->mixer_effects.musicQueue,
            ctx->mixer_effects.music, ctx->dst_sample_rate, ctx->dst_channels);
    }
    short *voice_bgm_music_buffer = mixer_combine(ctx, voice_bgm_buffer,
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
    int encoder_type, const char *out_file_path) {
    LogInfo("%s.\n", __func__);
    int ret = -1;
    short *buffer = NULL;
    if (!ctx || !out_file_path || !ctx->parser) {
        return ret;
    }

    ctx->muxer = open_muxer(ctx->dst_sample_rate, ctx->dst_channels,
        out_file_path, encoder_type);
    if (!ctx->muxer)
    {
        LogError("%s open_muxer failed.\n", __func__);
        ret = AEERROR_NOMEM;
        goto fail;
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

        ret = muxer_write_audio_frame(ctx->muxer, buffer, ret);
        if (ret < 0) {
            LogError("muxer_write_audio_frame failed\n");
            goto fail;
        }
    }

fail:
    if (buffer != NULL) {
        free(buffer);
        buffer = NULL;
    }
    muxer_stop(ctx->muxer);
    muxer_freep(&(ctx->muxer));
    return 0;
}

int xm_audio_mixer_mix(XmMixerContext *ctx,
    const char *out_file_path, int encoder_type)
{
    LogInfo("%s out_file_path = %s, encoder_type = %d.\n", __func__, out_file_path, encoder_type);
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

    if ((ret = xm_audio_mixer_mix_l(ctx, encoder_type, out_file_path)) < 0) {
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
        const char *in_pcm_path, const char *in_config_path)
{
    int ret = -1;
    if (!ctx || !in_config_path) {
        return ret;
    }
    LogInfo("%s in_config_path = %s.\n", __func__, in_config_path);

    mixer_free_l(ctx);
    ctx->dst_channels = DEFAULT_CHANNEL_NUMBER;
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
    ctx->pcm_sample_rate = ctx->mixer_effects.record->sample_rate;
    ctx->pcm_channels = ctx->mixer_effects.record->nb_channels;
    ctx->dst_sample_rate = ctx->mixer_effects.record->sample_rate;

    if (in_pcm_path == NULL) in_pcm_path = ctx->mixer_effects.record->file_path;
    if ((ctx->parser = pcm_parser_create(in_pcm_path, ctx->pcm_sample_rate,
	    ctx->pcm_channels, ctx->dst_sample_rate, ctx->dst_channels)) == NULL) {
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
