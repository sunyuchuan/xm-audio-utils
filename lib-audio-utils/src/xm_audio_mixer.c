#include "xm_audio_mixer.h"
#include "json/json_parse.h"
#include "codec/audio_muxer.h"
#include <stdio.h>
#include <pthread.h>
#include "voice_mixer_struct.h"
#include "effects/beautify/limiter.h"
#include "mixer_effects/side_chain_compress.h"
#include "error_def.h"
#include "log.h"
#include "tools/util.h"
#include "tools/fifo.h"

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
    // input pcm file fopen handle
    FILE *reader;
    int raw_pcm_buffer_size;
    short *raw_pcm_buffer;
    short *middle_buffer[NB_MIDDLE_BUFFERS];
    float flp_buffer[MAX_NB_SAMPLES];
    fifo *audio_fifo;
    Limiter *limiter;
    AudioMuxer *muxer ;
    pthread_mutex_t mutex;
    MixerEffcets mixer_effects;
};

static int64_t align(int64_t x, int align) {
    return ((( x ) + (align) - 1) / (align) * (align));
}

static void bgm_music_data_free(int nb, BgmMusic **data) {
    LogInfo("%s\n", __func__);
    if (data) {
        for (int i = 0; i < nb; i++) {
            if (data[i]) {
                if (data[i]->url) free(data[i]->url);
                xm_audio_decoder_freep(&(data[i]->decoder));
                free(data[i]);
                data[i] = NULL;
            }
        }
    }
}

static void mixer_effects_free(MixerEffcets *mixer) {
    LogInfo("%s\n", __func__);
    if (NULL == mixer)
        return;

    if (mixer->bgms) {
        bgm_music_data_free(mixer->nb_bgms, mixer->bgms);
        free(mixer->bgms);
        mixer->bgms = NULL;
    }

    if (mixer->musics) {
        bgm_music_data_free(mixer->nb_musics, mixer->musics);
        free(mixer->musics);
        mixer->musics = NULL;
    }
}

static void short_to_flp(float *float_buffer, short *src_buffer,
        int buffer_size) {
    if (!float_buffer || !src_buffer)
        return;

    for (int i = 0; i < buffer_size; i++) {
        float_buffer[i] = src_buffer[i] / (float)32767;
    }
}

static void flp_to_short(short *short_buffer, float *src_buffer,
        int buffer_size) {
    if (!short_buffer || !src_buffer)
        return;

    for (int i = 0; i < buffer_size; i++) {
        short_buffer[i] = (short)(src_buffer[i] * 32767);
    }
}

static void limiter(Limiter *limiter, short *short_buffer,
    float *flp_buffer, int buffer_size) {
    if (!limiter || !short_buffer || !flp_buffer)
        return;

    short_to_flp(flp_buffer, short_buffer, buffer_size);
    LimiterProcess(limiter, flp_buffer, buffer_size);
    flp_to_short(short_buffer, flp_buffer, buffer_size);
}

static AudioDecoder *open_decoder(BgmMusic *bgm_music,
        int dst_sample_rate, int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!bgm_music || !bgm_music->url)
        return NULL;
    AudioDecoder *decoder = NULL;

    decoder = xm_audio_decoder_create(bgm_music->url,
        dst_sample_rate, dst_channels);
    if (!decoder)
    {
        LogError("%s malloc bgm_music decoder failed.\n", __func__);
        return NULL;
    }
    bgm_music->fade_io.fade_in_nb_samples = bgm_music->fade_io.fade_in_time_ms * dst_sample_rate / 1000;
    bgm_music->fade_io.fade_out_nb_samples = bgm_music->fade_io.fade_out_time_ms * dst_sample_rate / 1000;
    bgm_music->yl_prev = bgm_music->makeup_gain * MAKEUP_GAIN_MAX_DB;
    xm_audio_decoder_seekTo(decoder, seek_time_ms);
    return decoder;
}

static BgmMusic *update_bgm_music(BgmMusic **bgm_music,
        int bgm_music_index, int nb_bgm_music,
        int dst_sample_rate, int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!bgm_music || !*bgm_music)
        return NULL;

    int index = bgm_music_index;
    if (index < nb_bgm_music && bgm_music[index]) {
        if (bgm_music[index]->decoder) {
            xm_audio_decoder_freep(&(bgm_music[index]->decoder));
        }
        bgm_music[index]->decoder = open_decoder(bgm_music[index],
            dst_sample_rate, dst_channels, seek_time_ms);
        if (!(bgm_music[index]->decoder))
        {
            LogError("%s bgm_music open_decoder failed, url: %s.\n", __func__, bgm_music[index]->url);
            return NULL;
        }
        return bgm_music[index];
    }

    return NULL;
}

static void bgm_music_seekTo(BgmMusic **bgm_music,
        int *bgm_music_index, int nb_bgm_music,
        int dst_sample_rate, int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!bgm_music || !*bgm_music)
        return;

    int bgm_seek_time = 0;
    for (int i = 0; i < nb_bgm_music; i++) {
        if (bgm_music[i] && bgm_music[i]->start_time_ms <= seek_time_ms) {
            if (bgm_music[i]->end_time_ms <= seek_time_ms) {
                *bgm_music_index = i + 1;
                bgm_seek_time = 0;
            } else {
                *bgm_music_index = i;
                bgm_seek_time = seek_time_ms - bgm_music[i]->start_time_ms;
                break;
            }
        }
    }

    update_bgm_music(bgm_music, *bgm_music_index, nb_bgm_music,
        dst_sample_rate, dst_channels, bgm_seek_time);
}

static void fade_in_out(BgmMusic *bgm_music, int sample_rate,
        int channels, int pcm_start_time, int pcm_duration,
        short *dst_buffer, int dst_buffer_size) {
    if (!bgm_music || !dst_buffer)
        return;

    check_fade_in_out(&(bgm_music->fade_io), pcm_start_time, pcm_duration,
        sample_rate, bgm_music->start_time_ms, bgm_music->end_time_ms);
    set_gain_s16(&(bgm_music->fade_io), dst_buffer,
        dst_buffer_size / channels, channels, bgm_music->volume);
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
    memset(&(ctx->mixer_effects), 0,  sizeof(MixerEffcets));

    muxer_stop(ctx->muxer);
    muxer_freep(&(ctx->muxer));

    if (ctx->limiter) {
        LimiterFree(&(ctx->limiter));
        ctx->limiter = NULL;
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

    if (ctx->raw_pcm_buffer) {
        free(ctx->raw_pcm_buffer);
        ctx->raw_pcm_buffer = NULL;
    }

    if (ctx->reader) {
        fclose(ctx->reader);
        ctx->reader = NULL;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->abort= false;
    ctx->progress = 0;
    pthread_mutex_unlock(&ctx->mutex);
}

static short *mixer_mix(XmMixerContext *ctx, short *pcm_buffer,
        int pcm_buffer_size, int pcm_start_time, int pcm_duration,
        BgmMusic *bgm_music, short *decoder_buffer, short *dst_buffer) {
    if (!ctx || !pcm_buffer || !bgm_music
            || !decoder_buffer || !dst_buffer)
        return NULL;

    short *mix_buffer = NULL;
    int dst_sample_rate = ctx->dst_sample_rate;
    int dst_channels = ctx->dst_channels;
    AudioDecoder *decoder = bgm_music->decoder;

    if (pcm_start_time >= bgm_music->start_time_ms &&
            pcm_start_time + pcm_duration < bgm_music->end_time_ms) {
        int decoder_buffer_size = xm_audio_decoder_get_decoded_frame(decoder,
                decoder_buffer, pcm_buffer_size, true);
        if (decoder_buffer_size <= 0) {
            LogWarning("%s 1 decoder_get_decoded_frame size is zero.\n", __func__);
            mix_buffer = pcm_buffer;
            goto end;
        }

        fade_in_out(bgm_music, dst_sample_rate, dst_channels,
                pcm_start_time, pcm_duration, decoder_buffer, decoder_buffer_size);
        if (bgm_music->side_chain_enable) {
            side_chain_compress(pcm_buffer, decoder_buffer, &(bgm_music->yl_prev),
                    decoder_buffer_size, dst_sample_rate, dst_channels,
                    SIDE_CHAIN_THRESHOLD, SIDE_CHAIN_RATIO, SIDE_CHAIN_ATTACK_MS,
                    SIDE_CHAIN_RELEASE_MS, bgm_music->makeup_gain);
        }
        MixBufferS16(pcm_buffer, decoder_buffer, decoder_buffer_size / dst_channels,
                dst_channels, dst_buffer, &(bgm_music->left_factor), &(bgm_music->right_factor));

        if (decoder_buffer_size < pcm_buffer_size) {
            memcpy(dst_buffer + decoder_buffer_size, pcm_buffer + decoder_buffer_size,
                sizeof(short) * (pcm_buffer_size - decoder_buffer_size));
        }
        mix_buffer = dst_buffer;
    } else if (pcm_start_time < bgm_music->start_time_ms &&
            pcm_start_time + pcm_duration > bgm_music->start_time_ms) {
        int decoder_start_index = ((bgm_music->start_time_ms - pcm_start_time) / (float)1000)
            * dst_sample_rate * dst_channels;
        memcpy(dst_buffer, pcm_buffer, sizeof(short) * decoder_start_index);

        int decoder_buffer_size = xm_audio_decoder_get_decoded_frame(decoder,
            decoder_buffer, pcm_buffer_size - decoder_start_index, true);
        if (decoder_buffer_size <= 0) {
            LogWarning("%s 2 decoder_get_decoded_frame size is zero.\n", __func__);
            mix_buffer = pcm_buffer;
            goto end;
        }

        fade_in_out(bgm_music, dst_sample_rate, dst_channels,
                pcm_start_time, pcm_duration, decoder_buffer, decoder_buffer_size);
        if (bgm_music->side_chain_enable) {
            side_chain_compress(pcm_buffer + decoder_start_index, decoder_buffer, &(bgm_music->yl_prev),
                    decoder_buffer_size, dst_sample_rate, dst_channels,
                    SIDE_CHAIN_THRESHOLD, SIDE_CHAIN_RATIO, SIDE_CHAIN_ATTACK_MS,
                    SIDE_CHAIN_RELEASE_MS, bgm_music->makeup_gain);
        }
        MixBufferS16(pcm_buffer + decoder_start_index, decoder_buffer,
            decoder_buffer_size / dst_channels, dst_channels,
            dst_buffer + decoder_start_index,
            &(bgm_music->left_factor), &(bgm_music->right_factor));

        if (decoder_buffer_size < (pcm_buffer_size - decoder_start_index)) {
            memcpy(dst_buffer + decoder_start_index + decoder_buffer_size,
                pcm_buffer + decoder_start_index + decoder_buffer_size, sizeof(short)
                * (pcm_buffer_size - decoder_start_index - decoder_buffer_size));
        }
        mix_buffer = dst_buffer;
    } else if (pcm_start_time <= bgm_music->end_time_ms &&
            pcm_start_time + pcm_duration > bgm_music->end_time_ms) {
        int decoder_size_in_short = ((bgm_music->end_time_ms - pcm_start_time) / (float)1000)
            * dst_sample_rate * dst_channels;
        int decoder_buffer_size = xm_audio_decoder_get_decoded_frame(decoder,
            decoder_buffer, decoder_size_in_short, true);
        if (decoder_buffer_size <= 0) {
            LogWarning("%s 3 decoder_get_decoded_frame size is zero.\n", __func__);
            mix_buffer = pcm_buffer;
            //update the decoder that point the next bgm
            xm_audio_decoder_freep(&(bgm_music->decoder));
            goto end;
        }

        fade_in_out(bgm_music, dst_sample_rate, dst_channels,
                pcm_start_time, pcm_duration, decoder_buffer, decoder_buffer_size);
        if (bgm_music->side_chain_enable) {
            side_chain_compress(pcm_buffer, decoder_buffer, &(bgm_music->yl_prev),
                    decoder_buffer_size, dst_sample_rate, dst_channels,
                    SIDE_CHAIN_THRESHOLD, SIDE_CHAIN_RATIO, SIDE_CHAIN_ATTACK_MS,
                    SIDE_CHAIN_RELEASE_MS, bgm_music->makeup_gain);
        }
        MixBufferS16(pcm_buffer, decoder_buffer, decoder_buffer_size / dst_channels,
            dst_channels, dst_buffer, &(bgm_music->left_factor), &(bgm_music->right_factor));

        memcpy(dst_buffer + decoder_buffer_size, pcm_buffer + decoder_buffer_size,
            sizeof(short) * (pcm_buffer_size - decoder_buffer_size));
        mix_buffer = dst_buffer;
        //update the decoder that point the next bgm
        xm_audio_decoder_freep(&(bgm_music->decoder));
    } else {
        mix_buffer = pcm_buffer;
    }

end:
    return mix_buffer;
}

static int mixer_mix_and_write_fifo(XmMixerContext *ctx) {
    int ret = -1;
    if (!ctx)
        return ret;

    if (feof(ctx->reader) || ferror(ctx->reader)) {
        ret = -1;
        goto end;
    }

    int buffer_start_ms = ctx->seek_time_ms +
        1000 * ((float)ctx->cur_size / ctx->pcm_channels / ctx->pcm_sample_rate);
    int read_len = fread(ctx->raw_pcm_buffer, 2, ctx->raw_pcm_buffer_size,
        ctx->reader);
    if (read_len <= 0) {
        ret = read_len;
        goto end;
    }
    int duration =
        1000 * ((float)read_len / ctx->pcm_channels / ctx->pcm_sample_rate);
    ctx->cur_size += read_len;

    int stereo_pcm_buffer_size = 0;
    short *stereo_pcm_buffer = NULL;
    if (ctx->pcm_channels == 1) {
        MonoToStereoS16(ctx->middle_buffer[VoicePcm], ctx->raw_pcm_buffer, read_len);
        stereo_pcm_buffer_size = read_len * 2;
        stereo_pcm_buffer = ctx->middle_buffer[VoicePcm];
    } else if (ctx->pcm_channels == 2) {
        stereo_pcm_buffer_size = read_len;
        stereo_pcm_buffer = ctx->raw_pcm_buffer;
    } else {
        LogError("unsupport pcm_channels : %d.\n", ctx->pcm_channels);
        goto end;
    }

    short *voice_bgm_buffer = stereo_pcm_buffer;
    BgmMusic *bgm = NULL;
    if (ctx->mixer_effects.bgms_index < ctx->mixer_effects.nb_bgms) {
        bgm = ctx->mixer_effects.bgms[ctx->mixer_effects.bgms_index];
    }
    if (bgm) {
        voice_bgm_buffer = mixer_mix(ctx, stereo_pcm_buffer,
            stereo_pcm_buffer_size, buffer_start_ms, duration, bgm,
            ctx->middle_buffer[Decoder], ctx->middle_buffer[MixBgm]);
        if (voice_bgm_buffer == NULL) {
            LogError("mixing voice and bgm failed.\n");
            goto end;
        }
        if (bgm->decoder == NULL) {
            ctx->mixer_effects.bgms_index ++;
            bgm = update_bgm_music(ctx->mixer_effects.bgms,
                ctx->mixer_effects.bgms_index, ctx->mixer_effects.nb_bgms,
                ctx->dst_sample_rate, ctx->dst_channels, 0);
        }
    }

    short *voice_bgm_music_buffer = voice_bgm_buffer;
    BgmMusic *music = NULL;
    if (ctx->mixer_effects.musics_index < ctx->mixer_effects.nb_musics) {
        music = ctx->mixer_effects.musics[ctx->mixer_effects.musics_index];
    }
    if (music) {
        voice_bgm_music_buffer = mixer_mix(ctx, voice_bgm_buffer,
            stereo_pcm_buffer_size, buffer_start_ms, duration, music,
            ctx->middle_buffer[Decoder], ctx->middle_buffer[MixMusic]);
        if (voice_bgm_music_buffer == NULL) {
            LogError("mixing voice_bgm and music failed.\n");
            goto end;
        }
        if (music->decoder == NULL) {
            ctx->mixer_effects.musics_index ++;
            music = update_bgm_music(ctx->mixer_effects.musics,
                ctx->mixer_effects.musics_index, ctx->mixer_effects.nb_musics,
                ctx->dst_sample_rate, ctx->dst_channels, 0);
        }
    }

    limiter(ctx->limiter, voice_bgm_music_buffer, ctx->flp_buffer, stereo_pcm_buffer_size);
    ret = fifo_write(ctx->audio_fifo, voice_bgm_music_buffer, stereo_pcm_buffer_size);
    if (ret < 0) goto end;
    ret = stereo_pcm_buffer_size;

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
    if (!ctx || !buffer || buffer_size_in_short <= 0)
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
    if (!ctx || !ctx->reader)
        return -1;

    ctx->seek_time_ms = seek_time_ms > 0 ? seek_time_ms : 0;
    if (ctx->audio_fifo) fifo_clear(ctx->audio_fifo);

    //The offset needs to be a multiple of 2, because the pcm data is 16-bit.
    //The size of seek is in pcm data.
    int64_t offset = align(2 * ((int64_t) ctx->seek_time_ms * ctx->pcm_channels) *
        (ctx->pcm_sample_rate / (float) 1000), 2);
    LogInfo("%s fseek offset %"PRId64".\n", __func__, offset);
    int ret = fseek(ctx->reader, offset, SEEK_SET);
    ctx->cur_size = 0;

    bgm_music_seekTo(ctx->mixer_effects.bgms, &(ctx->mixer_effects.bgms_index),
        ctx->mixer_effects.nb_bgms, ctx->dst_sample_rate, ctx->dst_channels, ctx->seek_time_ms);
    bgm_music_seekTo(ctx->mixer_effects.musics, &(ctx->mixer_effects.musics_index),
        ctx->mixer_effects.nb_musics, ctx->dst_sample_rate, ctx->dst_channels, ctx->seek_time_ms);
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
    if (NULL == ctx || NULL == out_file_path) {
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

    fseek(ctx->reader, 0, SEEK_END);
    int64_t file_size = ftell(ctx->reader);
    fseek(ctx->reader, 0, SEEK_SET);
    ctx->seek_time_ms = 0;
    ctx->cur_size = 0;
    while (!feof(ctx->reader) && !ferror(ctx->reader) && !ctx->abort) {
        int progress = ((float)2*ctx->cur_size / file_size) * 100;
        pthread_mutex_lock(&ctx->mutex);
        ctx->progress = progress;
        pthread_mutex_unlock(&ctx->mutex);

        ret = xm_audio_mixer_get_frame(ctx, buffer, MAX_NB_SAMPLES);
        if (ret <= 0) {
            continue;
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
        const char *in_pcm_path, int pcm_sample_rate, int pcm_channels,
        const char *in_config_path)
{
    int ret = -1;
    if (!ctx || !in_config_path || !in_pcm_path) {
        return ret;
    }
    LogInfo("%s in_pcm_path = %s in_config_path = %s.\n", __func__, in_pcm_path, in_config_path);

    mixer_free_l(ctx);
    ctx->pcm_sample_rate = pcm_sample_rate;
    ctx->pcm_channels = pcm_channels;
    ctx->dst_sample_rate = pcm_sample_rate;
    ctx->dst_channels = DEFAULT_CHANNEL_NUMBER;
    ctx->cur_size = 0;
    ctx->seek_time_ms = 0;
    ctx->raw_pcm_buffer_size = 0;

    if ((ret = mixer_parse(&(ctx->mixer_effects), in_config_path)) < 0) {
        LogError("%s mixer_parse %s failed\n", __func__, in_config_path);
        goto fail;
    }

    if ((ret = ae_open_file(&ctx->reader, in_pcm_path, false)) < 0) {
	LogError("%s read input pcm file %s failed\n", __func__, in_pcm_path);
	goto fail;
    }

    if (ctx->mixer_effects.bgms && ctx->mixer_effects.bgms[0]) {
        BgmMusic *bgm = ctx->mixer_effects.bgms[0];
        if (bgm->decoder) {
            xm_audio_decoder_freep(&bgm->decoder);
        }
        bgm->decoder = open_decoder(bgm, ctx->dst_sample_rate, ctx->dst_channels, 0);
        if (!bgm->decoder)
        {
            LogError("%s open bgm decoder failed, url: %s.\n", __func__, bgm->url);
            ret = AEERROR_NOMEM;
            goto fail;
        }
        ctx->mixer_effects.bgms_index = 0;
    }

    if (ctx->mixer_effects.musics && ctx->mixer_effects.musics[0]) {
        BgmMusic *music = ctx->mixer_effects.musics[0];
        if (music->decoder) {
            xm_audio_decoder_freep(&music->decoder);
        }
        music->decoder = open_decoder(music, ctx->dst_sample_rate, ctx->dst_channels, 0);
        if (!music->decoder)
        {
            LogError("%s open music decoder failed, url: %s.\n", __func__, music->url);
            ret = AEERROR_NOMEM;
            goto fail;
        }
        ctx->mixer_effects.musics_index = 0;
    }

    ctx->limiter = LimiterCreate(pcm_sample_rate);
    if (!ctx->limiter) {
        LogError("%s LimiterCreate failed.\n", __func__);
        ret = AEERROR_NOMEM;
        goto fail;
    }
    LimiterSetSwitch(ctx->limiter, 1);
    LimiterSet(ctx->limiter, -0.5f, 0.0f, 0.0f, 0.0f);

    if (pcm_channels == 1) {
        ctx->raw_pcm_buffer_size = MAX_NB_SAMPLES / 2;
    } else {
        ctx->raw_pcm_buffer_size = MAX_NB_SAMPLES;
    }
    ctx->raw_pcm_buffer = (short *)calloc(sizeof(short), ctx->raw_pcm_buffer_size);
    if (!ctx->raw_pcm_buffer) {
        LogError("%s calloc raw_pcm_buffer failed.\n", __func__);
        ret = AEERROR_NOMEM;
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
