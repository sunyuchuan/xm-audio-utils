#include <string.h>
#include <ctype.h>
#include "effects/voice_effect.h"
#include "log.h"
#include "xm_total_effects.h"

static void te_free(TotalEffectContext *ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx)
        return;

    for (short i = 0; i < MAX_NB_TOTAL_EFFECTS; ++i) {
        if (ctx->effects[i]) {
            free_effect(ctx->effects[i]);
            ctx->effects[i] = NULL;
        }
    }

    if (ctx->sdl_mutex)
        sdl_mutex_free(&ctx->sdl_mutex);
    if (ctx->fifo_in)
        fifo_delete(&ctx->fifo_in);
    if (ctx->fifo_out)
        fifo_delete(&ctx->fifo_out);
    if (ctx->buffer) {
        free(ctx->buffer);
        ctx->buffer = NULL;
    }
}

static int flush(TotalEffectContext *ctx,
    short *buffer, int buffer_size_in_short) {
    if (!ctx || !buffer || buffer_size_in_short <= 0)
        return -1;

    int receive_len = 0;
    for (int i = 0; i < MAX_NB_TOTAL_EFFECTS; ++i) {
        if (NULL == ctx->effects[i]) continue;

        bool is_last_effect = true;
        receive_len = receive_samples(ctx->effects[i],
            buffer, buffer_size_in_short);
        if (receive_len <= 0) {
            receive_len = flush_effect(ctx->effects[i],
                buffer, buffer_size_in_short);
        }
        while (receive_len > 0) {
            for (short j = i + 1; j < MAX_NB_TOTAL_EFFECTS; ++j) {
                if (NULL != ctx->effects[j]) {
                    receive_len = send_samples(ctx->effects[j],
                        buffer, receive_len);
                    if (receive_len < 0) {
                        LogError("%s send_samples to the next effect failed\n",__func__);
                        goto end;
                    }
                    is_last_effect = false;
                    receive_len = receive_samples(ctx->effects[i],
                        buffer, buffer_size_in_short);
                    if (receive_len <= 0) {
                        receive_len = flush_effect(ctx->effects[i],
                        buffer, buffer_size_in_short);
                    }
                    break;
                }
            }
            if (is_last_effect) break;
        }
    }

end:
    return receive_len;
}

static int add_effects(TotalEffectContext *ctx,
    short *buffer, int buffer_len) {
    int ret = -1;
    if (!ctx || !ctx->fifo_out
        || !buffer || buffer_len <= 0) return ret;

    bool find_valid_effect = false;
    for (int i = 0; i < MAX_NB_TOTAL_EFFECTS; ++i) {
        if (NULL != ctx->effects[i]) {
            if(send_samples(ctx->effects[i], buffer, buffer_len) < 0) {
                LogError("%s send_samples to the first effect failed\n",
                    __func__);
                ret = -1;
                return ret;
            }
            find_valid_effect = true;
            break;
        }
    }
    if (!find_valid_effect) {
        return buffer_len;
    }

    int receive_len = 0;
    for (int i = 0; i < MAX_NB_TOTAL_EFFECTS; ++i) {
        if (NULL == ctx->effects[i]) {
            continue;
        }

        bool is_last_effect = true;
        receive_len = receive_samples(ctx->effects[i],
            buffer, buffer_len);
        while (receive_len > 0) {
            for (short j = i + 1; j < MAX_NB_TOTAL_EFFECTS; ++j) {
                if (NULL != ctx->effects[j]) {
                    receive_len = send_samples(ctx->effects[j],
                        buffer, receive_len);
                    if (receive_len < 0) {
                        LogError("%s send_samples to the next effect failed\n",
                            __func__);
                        ret = -1;
                        return ret;
                    }
                    is_last_effect = false;
                    receive_len = receive_samples(ctx->effects[i],
                        buffer, buffer_len);
                    break;
                }
            }
            if (is_last_effect) break;
        }
    }

    return receive_len;
}

static char *strlower(char *str) {
    char *original = str;

    for (; *str!='\0'; str++)
        *str = tolower(*str);

    return original;
}

static int effects_init(TotalEffectContext *ctx,
    EffectsInfo *effects_info) {
    if (!ctx) return -1;

    for (int i = 0; i < MAX_NB_TOTAL_EFFECTS; ++i) {
        if (ctx->effects[i]) {
            free_effect(ctx->effects[i]);
            ctx->effects[i] = NULL;
        }
    }

    for (int i = 0; i < MAX_NB_TOTAL_EFFECTS; ++i) {
        if (!effects_info[i].name
            || !effects_info[i].info) continue;

        effects_info[i].name = strlower(effects_info[i].name);
        ctx->effects[i] = create_effect(
            find_effect(effects_info[i].name),
            ctx->sample_rate, ctx->channels);
        init_effect(ctx->effects[i], 0, NULL);
        set_effect(ctx->effects[i], effects_info[i].name,
            effects_info[i].info, 0);
    }

    return 0;
}

void total_effect_freep(TotalEffectContext **ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx || NULL == *ctx)
        return;

    te_free(*ctx);
    free(*ctx);
    *ctx = NULL;
}

int total_effect_flush(TotalEffectContext *ctx,
    short *buffer, int buffer_size_in_short) {
    if (!ctx || !buffer || buffer_size_in_short <= 0)
        return -1;

    sdl_mutex_lock(ctx->sdl_mutex);
    int ret = flush(ctx, ctx->buffer, MAX_NB_SAMPLES);
    if (ret > 0) {
        fifo_write(ctx->fifo_out, ctx->buffer, ret);
    }
    sdl_mutex_unlock(ctx->sdl_mutex);
    return fifo_read(ctx->fifo_out, buffer, buffer_size_in_short);
}

int total_effect_receive(TotalEffectContext *ctx,
    short *buffer, int buffer_size_in_short) {
    if(!ctx || !buffer
        || buffer_size_in_short <= 0) return AEERROR_NULL_POINT;
    if (!ctx->fifo_in || !ctx->fifo_out
        || !ctx->buffer) return AEERROR_NULL_POINT;

    int ret = -1;
    sdl_mutex_lock(ctx->sdl_mutex);
    size_t nb_samples =
        fifo_read(ctx->fifo_in, ctx->buffer, MAX_NB_SAMPLES);
    while (nb_samples > 0) {
        ret = add_effects(ctx, ctx->buffer, nb_samples);
        if (ret < 0) {
            LogError("add_effects error.\n");
            goto fail;
        }
        fifo_write(ctx->fifo_out, ctx->buffer, ret);
        nb_samples =
            fifo_read(ctx->fifo_in, ctx->buffer, MAX_NB_SAMPLES);
    }
    sdl_mutex_unlock(ctx->sdl_mutex);

    return fifo_read(ctx->fifo_out, buffer, buffer_size_in_short);
fail:
    return ret;
}

int total_effect_send(TotalEffectContext *ctx,
    short *buffer, int buffer_size_in_short) {
    if(!ctx || !buffer || !ctx->fifo_in
        || buffer_size_in_short <= 0)
        return AEERROR_NULL_POINT;
    int ret = -1;

    sdl_mutex_lock(ctx->sdl_mutex);
    ret = fifo_write(ctx->fifo_in, buffer, buffer_size_in_short);
    sdl_mutex_unlock(ctx->sdl_mutex);
    return ret;
}

int total_effect_init(TotalEffectContext *ctx,
    EffectsInfo *effects_info, int sample_rate,
    int channels, int bits_per_sample) {
    int ret = -1;
    if (!ctx || !effects_info)
        return ret;

    ctx->sample_rate = sample_rate;
    ctx->channels = channels;
    ctx->bits_per_sample = bits_per_sample;

    ctx->fifo_in = fifo_create(bits_per_sample >> 3);
    if (NULL == ctx->fifo_in) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    ctx->fifo_out = fifo_create(bits_per_sample >> 3);
    if (NULL == ctx->fifo_out) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    ctx->buffer = calloc((size_t)MAX_NB_SAMPLES, sizeof(*ctx->buffer));
    if (NULL == ctx->buffer) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    ctx->sdl_mutex = sdl_mutex_create();
    if (NULL == ctx->sdl_mutex) {
        ret = AEERROR_NOMEM;
        goto end;
    }

    return effects_init(ctx, effects_info);
end:
    return ret;
}

TotalEffectContext *total_effect_create() {
    TotalEffectContext *self =
        (TotalEffectContext *)calloc(1, sizeof(TotalEffectContext));
    if (NULL == self) {
        LogError("%s alloc TotalEffectContext failed.\n", __func__);
        return NULL;
    }

    return self;
}
