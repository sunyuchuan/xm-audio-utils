#include "xm_audio_effects.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include "log.h"
#include "tools/util.h"
#include "json/json_parse.h"
#include "voice_mixer_struct.h"
#include "effects/voice_effect.h"

struct XmEffectContext_T {
    int ae_status;
    int progress;
    int sample_rate;
    int channels;
    int16_t read_buffer[MAX_NB_SAMPLES];
    int16_t write_buffer[MAX_NB_SAMPLES];
    pthread_mutex_t mutex;
    VoiceEffcets voice_effects;
    volatile bool abort;
};

static void voice_effects_free(VoiceEffcets *voice) {
    LogInfo("%s\n", __func__);
    if (!voice) {
        return;
    }

    for (short i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (voice->effects[i]) {
            free_effect(voice->effects[i]);
            voice->effects[i] = NULL;
        }
    }
}

static int add_voice_effects(XmEffectContext *ctx, FILE *reader, FILE *writer) {
    int64_t cur_size = 0, ret = -1;
    VoiceEffcets *voice_effects = &(ctx->voice_effects);
    fseek(reader, 0, SEEK_END);
    int64_t file_size = ftell(reader);
    fseek(reader, 0, SEEK_SET);
    while (!feof(reader) && !ferror(reader) && !ctx->abort) {
        int read_len = fread(ctx->read_buffer, 2, MAX_NB_SAMPLES, reader);
        if (read_len <= 0)
            continue;

        cur_size += read_len;
        int progress = ((float)2*cur_size / file_size) * 100;
        pthread_mutex_lock(&ctx->mutex);
        ctx->progress = progress;
        pthread_mutex_unlock(&ctx->mutex);

        for (short i = voice_effects->first_valid_effect_index; i <= voice_effects->final_valid_effect_index; ++i) {
            if (NULL != voice_effects->effects[i]) {
                if(send_samples(voice_effects->effects[i], ctx->read_buffer, read_len) < 0) {
                    LogError("%s send_samples to the first effect failed\n", __func__);
                    ret = -1;
                    goto fail;
                }
                break;
            }
        }

        int receive_len = 0;
        for (short i = voice_effects->first_valid_effect_index; i < voice_effects->final_valid_effect_index; ++i) {
            if (NULL == voice_effects->effects[i])
                continue;
            receive_len = receive_samples(voice_effects->effects[i], ctx->write_buffer, MAX_NB_SAMPLES);
            while (receive_len > 0) {
                for (short j = i + 1; j <= voice_effects->final_valid_effect_index; ++j) {
                    if (NULL != voice_effects->effects[j]) {
                        receive_len = send_samples(voice_effects->effects[j], ctx->write_buffer, receive_len);
                        break;
                    }
                }
                if (receive_len < 0) {
                    LogError("%s send_samples to the next effect failed\n", __func__);
                    ret = -1;
                    goto fail;
                }

                receive_len = receive_samples(voice_effects->effects[i], ctx->write_buffer, MAX_NB_SAMPLES);
            }
        }

        if (voice_effects->effects[voice_effects->final_valid_effect_index]) {
            receive_len = receive_samples(voice_effects->effects[voice_effects->final_valid_effect_index], ctx->write_buffer, MAX_NB_SAMPLES);
        }

        fwrite(ctx->write_buffer, 2, receive_len, writer);
    }

    ret = 0;
fail:
    return ret;
}

static int xmctx_chkst_l(int ae_state)
{
    if (ae_state == AE_STATE_INITIALIZED
         || ae_state == AE_STATE_COMPLETED
         || ae_state == AE_STATE_ERROR) {
        return 0;
    }

    LogError("%s xmctx state(%d) is invalid\n", __func__, ae_state);
    return -1;
}

static void xmctx_abort_l(XmEffectContext *ctx)
{
    if(!ctx)
        return;

    pthread_mutex_lock(&ctx->mutex);
    ctx->abort= true;
    pthread_mutex_unlock(&ctx->mutex);
}

static void xmctx_reset_l(XmEffectContext *ctx)
{
    if(!ctx)
        return;

    voice_effects_free(&(ctx->voice_effects));
    ctx->voice_effects.nb_effects = 0;
    ctx->voice_effects.final_valid_effect_index = 0;
    ctx->voice_effects.first_valid_effect_index = MAX_NB_EFFECTS - 1;
    memset(ctx->voice_effects.effects, 0, MAX_NB_EFFECTS * sizeof(EffectContext *));
    memset(ctx->read_buffer, 0, MAX_NB_SAMPLES * sizeof(int16_t));
    memset(ctx->write_buffer, 0, MAX_NB_SAMPLES * sizeof(int16_t));
    pthread_mutex_lock(&ctx->mutex);
    ctx->abort= false;
    ctx->progress = 0;
    pthread_mutex_unlock(&ctx->mutex);
}

void xm_audio_effect_freep(XmEffectContext **ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx || NULL == *ctx)
        return;
    XmEffectContext *self = *ctx;

    voice_effects_free(&(self->voice_effects));
    pthread_mutex_destroy(&self->mutex);
    free(*ctx);
    *ctx = NULL;
}

void xm_audio_effect_stop(XmEffectContext *ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx)
        return;

    xmctx_abort_l(ctx);
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

int xm_audio_effect_add(XmEffectContext *ctx, const char *in_pcm_path, const char *in_config_path,
                    const char *out_pcm_path) {
    LogInfo("%s in_pcm_path = %s in_config_path = %s.\n", __func__, in_pcm_path, in_config_path);
    LogInfo("%s out_pcm_path = %s.\n", __func__, out_pcm_path);
    int ret = -1;
    if(NULL == ctx || NULL == in_pcm_path
        || NULL == in_config_path || NULL == out_pcm_path) {
        return ret;
    }

    if (xmctx_chkst_l(ctx->ae_status) < 0) {
        return AEERROR_INVALID_STATE;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_STARTED;
    pthread_mutex_unlock(&ctx->mutex);
    xmctx_reset_l(ctx);

    FILE *reader = NULL;
    if ((ret = ae_open_file(&reader, in_pcm_path, 0)) < 0) {
        LogError("%s open input file %s failed\n", __func__, in_pcm_path);
        goto fail;
    }

    FILE *writer = NULL;
    if ((ret = ae_open_file(&writer, out_pcm_path, 1)) < 0) {
        LogError("%s open output file %s failed\n", __func__, out_pcm_path);
        goto fail;
    }

    if ((ret = effects_parse(&(ctx->voice_effects), in_config_path, ctx->sample_rate, ctx->channels)) < 0) {
        LogError("%s effects_parse %s failed\n", __func__, in_config_path);
        goto fail;
    }

    if ((ret = add_voice_effects(ctx, reader, writer)) < 0) {
        LogError("%s add_voice_effects failed\n", __func__);
        goto fail;
    }

fail:
    if (reader) {
        fclose(reader);
        reader = NULL;
    }
    if (writer) {
        fclose(writer);
        writer = NULL;
    }

    pthread_mutex_lock(&ctx->mutex);
    if (ret < 0) {
        ctx->ae_status = AE_STATE_ERROR;
    } else {
        ctx->ae_status = AE_STATE_COMPLETED;
    }
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
}

XmEffectContext *
xm_audio_effect_create(const int sample_rate, const int channels) {
    XmEffectContext *self = (XmEffectContext *)calloc(1, sizeof(XmEffectContext));
    if (NULL == self) {
        LogError("%s alloc XmEffectContext failed.\n", __func__);
        return NULL;
    }

    self->sample_rate = sample_rate;
    self->channels = channels;
    self->voice_effects.final_valid_effect_index = 0;
    self->voice_effects.first_valid_effect_index = MAX_NB_EFFECTS - 1;
    memset(self->voice_effects.effects, 0, MAX_NB_EFFECTS * sizeof(EffectContext *));
    pthread_mutex_init(&self->mutex, NULL);
    self->ae_status = AE_STATE_INITIALIZED;

    return self;
}

