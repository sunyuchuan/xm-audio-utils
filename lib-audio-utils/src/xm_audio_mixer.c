#include "xm_audio_mixer.h"
#include "pcm_parser.h"
#include "json/json_parse.h"
#include <pthread.h>
#include "voice_mixer_struct.h"
#include "effects/beautify/limiter.h"
#include "mixer_effects/side_chain_compress.h"
#include "error_def.h"
#include "log.h"
#include "tools/util.h"
#include "tools/fifo.h"
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
    float flp_buffer[MAX_NB_SAMPLES];
    fifo *audio_fifo;
    Limiter *limiter;
    pthread_mutex_t mutex;
    MixerEffcets mixer_effects;
};

static void bgm_music_data_free(int nb, BgmMusic **data) {
    LogInfo("%s\n", __func__);
    if (data) {
        for (int i = 0; i < nb; i++) {
            if (data[i]) {
                if (data[i]->url) free(data[i]->url);
                if (data[i]->parser) pcm_parser_freep(&data[i]->parser);
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

static void limiter(Limiter *limiter, short *short_buffer,
    float *flp_buffer, int buffer_size) {
    if (!limiter || !short_buffer || !flp_buffer)
        return;

    S16ToFloat(short_buffer, flp_buffer, buffer_size);
    LimiterProcess(limiter, flp_buffer, buffer_size);
    FloatToS16(flp_buffer, short_buffer, buffer_size);
}

static PcmParser *open_bgm_music_parser(BgmMusic *bgm_music,
        int dst_sample_rate, int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!bgm_music || !bgm_music->url)
        return NULL;

    if (bgm_music->parser) {
        pcm_parser_freep(&(bgm_music->parser));
    }

    if ((bgm_music->parser = pcm_parser_create(bgm_music->url,
            bgm_music->sample_rate, bgm_music->nb_channels,
            dst_sample_rate, dst_channels)) == NULL) {
	LogError("%s open bgm pcm parser failed, file addr %s.\n", __func__, bgm_music->url);
	return NULL;
    }

    bgm_music->fade_io.fade_in_nb_samples = bgm_music->fade_io.fade_in_time_ms * dst_sample_rate / 1000;
    bgm_music->fade_io.fade_out_nb_samples = bgm_music->fade_io.fade_out_time_ms * dst_sample_rate / 1000;
    bgm_music->yl_prev = bgm_music->makeup_gain * MAKEUP_GAIN_MAX_DB;

    pcm_parser_seekTo(bgm_music->parser, seek_time_ms);
    return bgm_music->parser;
}

static BgmMusic *update_bgm_music(BgmMusic **bgm_music,
        int bgm_music_index, int nb_bgm_music,
        int dst_sample_rate, int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!bgm_music || !*bgm_music)
        return NULL;

    int index = bgm_music_index;
    if (index < nb_bgm_music && bgm_music[index]) {
        PcmParser *parser = open_bgm_music_parser(bgm_music[index],
            dst_sample_rate, dst_channels, seek_time_ms);
        if (!parser)
        {
            LogError("%s open bgm_music parser failed, url: %s.\n", __func__, bgm_music[index]->url);
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
    memset(&(ctx->mixer_effects), 0,  sizeof(MixerEffcets));

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
        BgmMusic *bgm_music, short *decoder_buffer, short *dst_buffer) {
    if (!ctx || !pcm_buffer || !bgm_music || !decoder_buffer || !dst_buffer)
        return NULL;

    short *mix_buffer = NULL;
    int dst_sample_rate = ctx->dst_sample_rate;
    int dst_channels = ctx->dst_channels;
    PcmParser *parser = bgm_music->parser;

    if (pcm_start_time >= bgm_music->start_time_ms &&
            pcm_start_time + pcm_duration < bgm_music->end_time_ms) {
        int decoder_buffer_size = pcm_parser_get_pcm_frame(parser,
                decoder_buffer, pcm_buffer_size, true);
        if (decoder_buffer_size <= 0) {
            LogWarning("%s 1 pcm_parser_get_pcm_frame size is zero.\n", __func__);
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

        int decoder_buffer_size = pcm_parser_get_pcm_frame(parser,
            decoder_buffer, pcm_buffer_size - decoder_start_index, true);
        if (decoder_buffer_size <= 0) {
            LogWarning("%s 2 pcm_parser_get_pcm_frame size is zero.\n", __func__);
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
        int decoder_buffer_size = pcm_parser_get_pcm_frame(parser,
            decoder_buffer, decoder_size_in_short, true);
        if (decoder_buffer_size <= 0) {
            LogWarning("%s 3 pcm_parser_get_pcm_frame size is zero.\n", __func__);
            mix_buffer = pcm_buffer;
            //update the decoder that point the next bgm
            pcm_parser_freep(&(bgm_music->parser));
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
        pcm_parser_freep(&(bgm_music->parser));
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

    short *voice_bgm_buffer = ctx->middle_buffer[VoicePcm];
    BgmMusic *bgm = NULL;
    if (ctx->mixer_effects.bgms_index < ctx->mixer_effects.nb_bgms) {
        bgm = ctx->mixer_effects.bgms[ctx->mixer_effects.bgms_index];
    }
    if (bgm) {
        voice_bgm_buffer = mixer_mix(ctx, ctx->middle_buffer[VoicePcm],
            read_len, buffer_start_ms, duration, bgm,
            ctx->middle_buffer[Decoder], ctx->middle_buffer[MixBgm]);
        if (voice_bgm_buffer == NULL) {
            LogError("mixing voice and bgm failed.\n");
            goto end;
        }
        if (bgm->parser == NULL) {
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
            read_len, buffer_start_ms, duration, music,
            ctx->middle_buffer[Decoder], ctx->middle_buffer[MixMusic]);
        if (voice_bgm_music_buffer == NULL) {
            LogError("mixing voice_bgm and music failed.\n");
            goto end;
        }
        if (music->parser == NULL) {
            ctx->mixer_effects.musics_index ++;
            music = update_bgm_music(ctx->mixer_effects.musics,
                ctx->mixer_effects.musics_index, ctx->mixer_effects.nb_musics,
                ctx->dst_sample_rate, ctx->dst_channels, 0);
        }
    }

    limiter(ctx->limiter, voice_bgm_music_buffer, ctx->flp_buffer, read_len);
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

    bgm_music_seekTo(ctx->mixer_effects.bgms,
        &(ctx->mixer_effects.bgms_index), ctx->mixer_effects.nb_bgms,
        ctx->dst_sample_rate, ctx->dst_channels, ctx->seek_time_ms);
    bgm_music_seekTo(ctx->mixer_effects.musics,
        &(ctx->mixer_effects.musics_index), ctx->mixer_effects.nb_musics,
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

    if ((ret = mixer_parse(&(ctx->mixer_effects), in_config_path)) < 0) {
        LogError("%s mixer_parse %s failed\n", __func__, in_config_path);
        goto fail;
    }

    if ((ctx->parser = pcm_parser_create(in_pcm_path, pcm_sample_rate,
	    pcm_channels, dst_sample_rate, dst_channels)) == NULL) {
	LogError("%s pcm_parser_create failed, file addr : %s.\n", __func__, in_pcm_path);
	goto fail;
    }

    if (ctx->mixer_effects.bgms && ctx->mixer_effects.bgms[0]) {
        BgmMusic *bgm = ctx->mixer_effects.bgms[0];
        PcmParser *parser = open_bgm_music_parser(bgm, dst_sample_rate, dst_channels, 0);
        if (!parser)
        {
            LogError("%s open bgm parser failed, url: %s.\n", __func__, bgm->url);
            ret = AEERROR_NOMEM;
            goto fail;
        }
        ctx->mixer_effects.bgms_index = 0;
    }

    if (ctx->mixer_effects.musics && ctx->mixer_effects.musics[0]) {
        BgmMusic *music = ctx->mixer_effects.musics[0];
        PcmParser *parser = open_bgm_music_parser(music, dst_sample_rate, dst_channels, 0);
        if (!parser)
        {
            LogError("%s open music parser failed, url: %s.\n", __func__, music->url);
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
