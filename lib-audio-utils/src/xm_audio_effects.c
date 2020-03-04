#include "xm_audio_effects.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "tools/util.h"
#include "tools/fifo.h"
#include "json/json_parse.h"
#include "voice_mixer_struct.h"
#include "effects/voice_effect.h"
#include "pcm_parser.h"

#define DEFAULT_SAMPLE_RATE 44100
#define DEFAULT_CHANNEL_NUMBER 1

struct XmEffectContext_T {
    volatile bool abort;
    int ae_status;
    int progress;
    // input pcm sample rate and number channels
    int dst_sample_rate;
    int dst_channels;
    // input pcm file seek position
    int seek_time_ms;
    // input pcm read location
    int64_t cur_size;
    // input pcm file fopen handle
    PcmParser *parser;
    short buffer[MAX_NB_SAMPLES];
    fifo *audio_fifo;
    pthread_mutex_t mutex;
    VoiceEffcets voice_effects;
};

static void voice_effects_free(VoiceEffcets *voice) {
    LogInfo("%s\n", __func__);
    if (!voice) {
        return;
    }

    if (voice->record) {
        audio_record_source_freep(&voice->record);
    }

    for (short i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (voice->effects[i]) {
            free_effect(voice->effects[i]);
            voice->effects[i] = NULL;
        }
    }
}

static void ae_free(XmEffectContext *ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx)
        return;

    if (ctx->parser) {
        pcm_parser_freep(&ctx->parser);
    }
    if (ctx->audio_fifo) {
        fifo_delete(&ctx->audio_fifo);
    }
    voice_effects_free(&ctx->voice_effects);
}

static int ae_chkst_l(int ae_state)
{
    if (ae_state == AE_STATE_INITIALIZED) {
        return 0;
    }

    LogError("%s audio effect state(%d) is invalid.\n", __func__, ae_state);
    LogError("%s expecting ae_state == AE_STATE_INITIALIZED(1).\n", __func__);
    return -1;
}

static void ae_abort_l(XmEffectContext *ctx)
{
    if(!ctx)
        return;

    pthread_mutex_lock(&ctx->mutex);
    ctx->abort= true;
    pthread_mutex_unlock(&ctx->mutex);
}

static void ae_reset_l(XmEffectContext *ctx)
{
    if(!ctx)
        return;

    ae_abort_l(ctx);
    ae_free(ctx);

    memset(ctx->voice_effects.effects, 0, MAX_NB_EFFECTS * sizeof(EffectContext *));
    memset(ctx->buffer, 0, MAX_NB_SAMPLES * sizeof(*(ctx->buffer)));
    pthread_mutex_lock(&ctx->mutex);
    ctx->abort= false;
    ctx->progress = 0;
    pthread_mutex_unlock(&ctx->mutex);
}

static void flush(XmEffectContext *ctx) {
    LogInfo("%s start.\n", __func__);
    if (!ctx)
        return;

    VoiceEffcets *voice_effects = &ctx->voice_effects;
    while (1) {
        int receive_len = 0;
        for (int i = 0; i < MAX_NB_EFFECTS; ++i) {
            if (NULL == voice_effects->effects[i]) {
                continue;
            }

            bool is_last_effect = true;
            receive_len = receive_samples(voice_effects->effects[i], ctx->buffer, MAX_NB_SAMPLES);
            while (receive_len > 0) {
                for (short j = i + 1; j < MAX_NB_EFFECTS; ++j) {
                    if (NULL != voice_effects->effects[j]) {
                        receive_len = send_samples(voice_effects->effects[j], ctx->buffer, receive_len);
                        if (receive_len < 0) {
                            LogError("%s send_samples to the next effect failed\n", __func__);
                            goto end;
                        }
                        is_last_effect = false;
                        receive_len = receive_samples(voice_effects->effects[i], ctx->buffer, MAX_NB_SAMPLES);
                        break;
                    }
                }
                if (is_last_effect) break;
            }
        }

        if (receive_len > 0) {
            int ret = fifo_write(ctx->audio_fifo, ctx->buffer, receive_len);
            if (ret < 0) goto end;
        } else {
            goto end;
        }
    }
end:
    LogInfo("%s end.\n", __func__);
    return;
}

static int add_effects_and_write_fifo(XmEffectContext *ctx) {
    int ret = -1;
    if (!ctx)
        return ret;

    float cur_position = ctx->cur_size / (float)(ctx->parser->bits_per_sample / 8)
        / ctx->dst_channels / ctx->dst_sample_rate + ctx->seek_time_ms / (float)1000;
    if (1000 * cur_position > MAX_DURATION_MIX_IN_MS) {
        ret = PCM_FILE_EOF;
        goto end;
    }

    int read_len = pcm_parser_get_pcm_frame(ctx->parser,
        ctx->buffer, MAX_NB_SAMPLES, false);
    if (read_len < 0) {
        ret = read_len;
        goto end;
    }
    ctx->cur_size += (read_len * sizeof(*ctx->buffer));

    VoiceEffcets *voice_effects = &ctx->voice_effects;
    bool find_valid_effect = false;
    for (int i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (NULL != voice_effects->effects[i]) {
            if(send_samples(voice_effects->effects[i], ctx->buffer, read_len) < 0) {
                LogError("%s send_samples to the first effect failed\n", __func__);
                ret = -1;
                goto end;
            }
            find_valid_effect = true;
            break;
        }
    }

    if (!find_valid_effect) {
        ret = fifo_write(ctx->audio_fifo, ctx->buffer, read_len);
        if (ret < 0) goto end;
        return read_len;
    }

    int receive_len = 0;
    for (int i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (NULL == voice_effects->effects[i]) {
            continue;
        }

        bool is_last_effect = true;
        receive_len = receive_samples(voice_effects->effects[i], ctx->buffer, MAX_NB_SAMPLES);
        while (receive_len > 0) {
            for (short j = i + 1; j < MAX_NB_EFFECTS; ++j) {
                if (NULL != voice_effects->effects[j]) {
                    receive_len = send_samples(voice_effects->effects[j], ctx->buffer, receive_len);
                    if (receive_len < 0) {
                        LogError("%s send_samples to the next effect failed\n", __func__);
                        ret = -1;
                        goto end;
                    }
                    is_last_effect = false;
                    receive_len = receive_samples(voice_effects->effects[i], ctx->buffer, MAX_NB_SAMPLES);
                    break;
                }
            }
            if (is_last_effect) break;
        }
    }

    ret = fifo_write(ctx->audio_fifo, ctx->buffer, receive_len);
    if (ret < 0) goto end;
    ret = receive_len;
end:
    return ret;
}

void xm_audio_effect_freep(XmEffectContext **ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx || NULL == *ctx)
        return;
    XmEffectContext *self = *ctx;

    ae_abort_l(self);
    ae_free(self);
    pthread_mutex_destroy(&self->mutex);
    free(*ctx);
    *ctx = NULL;
}

void xm_audio_effect_stop(XmEffectContext *ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx)
        return;

    ae_abort_l(ctx);
}

int xm_audio_effect_get_progress(XmEffectContext *ctx) {
    if (NULL == ctx)
        return 0;

    int ret = 0;
    pthread_mutex_lock(&ctx->mutex);
    ret = ctx->progress;
    pthread_mutex_unlock(&ctx->mutex);

    return ret;
}

PcmParser *xm_audio_effect_get_pcm_parser(XmEffectContext *ctx) {
    if (!ctx) {
        return NULL;
    }

    return ctx->parser;
}

int xm_audio_effect_get_frame(XmEffectContext *ctx,
    short *buffer, int buffer_size_in_short) {
    int ret = -1;
    if (!ctx || !buffer || buffer_size_in_short <= 0)
        return ret;

    while (fifo_occupancy(ctx->audio_fifo) < (size_t) buffer_size_in_short) {
	ret = add_effects_and_write_fifo(ctx);
	if (ret < 0) {
	    flush(ctx);
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

int xm_audio_effect_seekTo(XmEffectContext *ctx,
        int seek_time_ms) {
    LogInfo("%s seek_time_ms %d.\n", __func__, seek_time_ms);
    if (!ctx || !ctx->parser)
        return -1;

    ctx->seek_time_ms = seek_time_ms > 0 ? seek_time_ms : 0;
    ctx->cur_size = 0;

    flush(ctx);
    if (ctx->audio_fifo) fifo_clear(ctx->audio_fifo);

    int ret = pcm_parser_seekTo(ctx->parser, ctx->seek_time_ms);
    return ret;
}

static int xm_audio_effect_add_effects_l(XmEffectContext *ctx,
    const char *out_pcm_path) {
    int ret = -1;
    if (!ctx || !out_pcm_path)
        return ret;

    FILE *writer = NULL;
    if ((ret = ae_open_file(&writer, out_pcm_path, true)) < 0) {
        LogError("%s open output file %s failed\n", __func__, out_pcm_path);
        goto fail;
    }

    WavContext *wav_ctx = &(ctx->voice_effects.record->wav_ctx);
    if (wav_write_header(writer, wav_ctx) < 0) {
        LogError("%s 1 write wav header failed, out_pcm_path %s\n", __func__, out_pcm_path);
    }

    ctx->seek_time_ms = 0;
    ctx->cur_size = 0;
    uint32_t data_size_byte = 0;
    float file_duration = ctx->parser->file_size / (float)(ctx->parser->bits_per_sample
        / 8) / ctx->parser->src_nb_channels/ ctx->parser->src_sample_rate_in_Hz;
    if (1000 * file_duration > MAX_DURATION_MIX_IN_MS) file_duration = MAX_DURATION_MIX_IN_MS / 1000;
    while (!ctx->abort) {
        float cur_position = ctx->cur_size / (float)(ctx->parser->bits_per_sample
            / 8) / ctx->dst_channels / ctx->dst_sample_rate + ctx->seek_time_ms / (float)1000;
        int progress = (cur_position / file_duration) * 100;
        pthread_mutex_lock(&ctx->mutex);
        ctx->progress = progress;
        pthread_mutex_unlock(&ctx->mutex);

        ret = xm_audio_effect_get_frame(ctx, ctx->buffer, MAX_NB_SAMPLES);
        if (ret <= 0) {
            LogInfo("xm_audio_effect_get_frame len <= 0.\n");
            break;
        }

        fwrite(ctx->buffer, sizeof(*(ctx->buffer)), ret, writer);
        data_size_byte += (ret * sizeof(*(ctx->buffer)));
    }

    wav_ctx->header.sample_rate = ctx->dst_sample_rate;
    wav_ctx->header.nb_channels = ctx->dst_channels;
    wav_ctx->header.bits_per_sample = ctx->parser->bits_per_sample;
    wav_ctx->header.block_align = ctx->dst_channels * (wav_ctx->header.bits_per_sample / 8);
    wav_ctx->header.byte_rate = wav_ctx->header.block_align * ctx->dst_sample_rate;
    wav_ctx->header.data_size = data_size_byte;
    // total file size minus the size of riff_id(4 byte) and riff_size(4 byte) itself
    wav_ctx->header.riff_size = wav_ctx->header.data_size +
        sizeof(wav_ctx->header) - 8;
    if (wav_write_header(writer, wav_ctx) < 0) {
        LogError("%s 2 write wav header failed, out_pcm_path %s\n", __func__, out_pcm_path);
    }

    if (ret == PCM_FILE_EOF) ret = 0;
fail:
    if (writer) {
        fclose(writer);
        writer = NULL;
    }
    return ret;
}

int xm_audio_effect_add_effects(XmEffectContext *ctx,
    const char *out_pcm_path) {
    LogInfo("%s out_pcm_path = %s.\n", __func__, out_pcm_path);
    int ret = -1;
    if (!ctx || !out_pcm_path) {
        return ret;
    }

    if (ae_chkst_l(ctx->ae_status) < 0) {
        return AEERROR_INVALID_STATE;
    }
    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_STARTED;
    pthread_mutex_unlock(&ctx->mutex);

    if ((ret = xm_audio_effect_add_effects_l(ctx, out_pcm_path)) < 0) {
        LogError("%s add_voice_effects failed\n", __func__);
        goto fail;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_COMPLETED;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
fail:
    ae_reset_l(ctx);
    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_ERROR;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
}

int xm_audio_effect_init(XmEffectContext *ctx,
    const char *in_config_path)
{
    int ret = -1;
    if (!ctx || !in_config_path) {
        return ret;
    }
    LogInfo("%s in_config_path = %s.\n", __func__, in_config_path);

    ae_reset_l(ctx);

    ctx->voice_effects.record = (AudioRecordSource *)calloc(1, sizeof(AudioRecordSource));
    if (NULL == ctx->voice_effects.record) {
        LogError("%s alloc AudioRecordSource failed.\n", __func__);
        goto fail;
    }

    if ((ret = effects_parse(&(ctx->voice_effects), in_config_path)) < 0) {
        LogError("%s effects_parse %s failed\n", __func__, in_config_path);
        goto fail;
    }
    ctx->seek_time_ms = 0;
    ctx->cur_size = 0;

    int src_sample_rate = ctx->voice_effects.record->sample_rate;
    int src_channels = ctx->voice_effects.record->nb_channels;
    int dst_sample_rate = ctx->voice_effects.dst_sample_rate;
    int dst_channels = ctx->voice_effects.dst_channels;
    char *in_pcm_path = ctx->voice_effects.record->file_path;
    if ((ctx->parser = pcm_parser_create(in_pcm_path, src_sample_rate,
            src_channels, dst_sample_rate, dst_channels,
            &(ctx->voice_effects.record->wav_ctx))) == NULL) {
        LogError("%s open pcm parser failed, file addr %s.\n", __func__, in_pcm_path);
        goto fail;
    }
    ctx->dst_sample_rate = ctx->parser->dst_sample_rate_in_Hz;
    ctx->dst_channels = ctx->parser->dst_nb_channels;

    // Allocate buffer for audio fifo
    ctx->audio_fifo = fifo_create(sizeof(short));
    if (!ctx->audio_fifo) {
        LogError("%s Could not allocate audio FIFO\n", __func__);
        ret = AEERROR_NOMEM;
        goto fail;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_INITIALIZED;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
fail:
    ae_reset_l(ctx);
    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_ERROR;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
}

XmEffectContext *xm_audio_effect_create() {
    XmEffectContext *self = (XmEffectContext *)calloc(1, sizeof(XmEffectContext));
    if (NULL == self) {
        LogError("%s alloc XmEffectContext failed.\n", __func__);
        return NULL;
    }

    self->dst_sample_rate = DEFAULT_SAMPLE_RATE;
    self->dst_channels = DEFAULT_CHANNEL_NUMBER;
    memset(self->voice_effects.effects, 0, MAX_NB_EFFECTS * sizeof(EffectContext *));
    pthread_mutex_init(&self->mutex, NULL);
    self->ae_status = AE_STATE_UNINIT;

    return self;
}
