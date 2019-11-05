//
// Created by layne on 19-5-20.
//
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsp_tools/fft/fft8g.h"
#include "effect_struct.h"
#include "error_def.h"
#include "log.h"
#include "math/fast_math_ops.h"
#include "noise_suppression/defines.h"
#include "noise_suppression/noise_estimation.h"
#include "noise_suppression/tables.h"
#include "tools/conversion.h"
#include "tools/fifo.h"
#include "tools/sdl_mutex.h"
#include "tools/util.h"

#define DC_REDUCTION

typedef struct {
    NoiseEstimation *noise_est;
    fifo *fifo_in;
    fifo *fifo_out;
    SdlMutex *sdl_mutex;
    float fft_array[FFT_LEN];
    float ns_ps[ACTUAL_LEN];
    float flp_pcm[FRAME_LEN];
    float last_half_fft_array[HALF_FFT_LEN];
    short raw_pcm[FFT_LEN];
    short fixed_pcm[FRAME_LEN];

    // FFT work arrays.
    int ip[FFT_LEN];
    float wfft[FFT_LEN];

    float gain_threshold_all;
    float gain_threshold_low;
    float gain_threshold_mid;
    float gain_threshold_high;
    float mid_freq_gain;
    short bandwidth_low2mid;
    short bandwidth_mid2high;
    short is_first_frame;
    bool is_noise_suppression_on;
    short is_enhance_mid_freq;
} priv_t;

static int noise_suppression_close(EffectContext *ctx) {
    LogInfo("%s.\n", __func__);
    assert(NULL != ctx);

    if (ctx->priv) {
        priv_t *priv = (priv_t *)ctx->priv;
        if (priv->noise_est) NoiseEstimationFree(&priv->noise_est);
        if (priv->fifo_in) fifo_delete(&priv->fifo_in);
        if (priv->fifo_out) fifo_delete(&priv->fifo_out);
        if (priv->sdl_mutex) sdl_mutex_free(&priv->sdl_mutex);
    }
    return 0;
}

static void init_self_parameter(priv_t *priv) {
    assert(NULL != priv);
    NoiseEstimationInit(priv->noise_est);
    memset(priv->fft_array, 0, FFT_LEN * sizeof(float));
    memset(priv->flp_pcm, 0, FRAME_LEN * sizeof(float));
    memset(priv->last_half_fft_array, 0, HALF_FFT_LEN * sizeof(float));
    memset(priv->raw_pcm, 0, FFT_LEN * sizeof(short));
    memset(priv->ns_ps, 0, ACTUAL_LEN * sizeof(float));
    memset(priv->fixed_pcm, 0, FRAME_LEN * sizeof(short));
    priv->ip[0] = 0;
    priv->ip[1] = 0;
    priv->gain_threshold_all = 0.25f;
    priv->gain_threshold_low = 1.0f;
    priv->gain_threshold_mid = 0.75f;
    priv->gain_threshold_high = 0.4f;
    priv->bandwidth_low2mid = 50;
    priv->bandwidth_mid2high = 250;
    priv->is_first_frame = 1;
    priv->is_noise_suppression_on = false;
    priv->is_enhance_mid_freq = 0;
    priv->mid_freq_gain = 0.5f;
}

static int noise_suppression_init(EffectContext *ctx, int argc, const char **argv) {
    LogInfo("%s.\n", __func__);
    for (int i = 0; i < argc; ++i) {
        LogInfo("argv[%d] = %s\n", i, argv[i]);
    }
    assert(NULL != ctx);
    priv_t *priv = (priv_t *)ctx->priv;
    if (NULL == priv) return AEERROR_NULL_POINT;

    int ret = 0;
    priv->noise_est = NoiseEstimationCreate();
    if (NULL == priv->noise_est) {
        ret = AEERROR_NOMEM;
        goto end;
    }

    priv->fifo_in = fifo_create(sizeof(int16_t));
    if (NULL == priv->fifo_in) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    priv->fifo_out = fifo_create(sizeof(int16_t));
    if (NULL == priv->fifo_out) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    priv->sdl_mutex = sdl_mutex_create();
    if (NULL == priv->sdl_mutex) {
        ret = AEERROR_NOMEM;
        goto end;
    }

    init_self_parameter(priv);

end:
    if (ret < 0) noise_suppression_close(ctx);
    return ret;
}

static int noise_suppression_set(EffectContext *ctx, const char *key,
                                 int flags) {
    assert(NULL != ctx);

    priv_t *priv = ctx->priv;
    AEDictionaryEntry *entry = ae_dict_get(ctx->options, key, NULL, flags);
    if (entry) {
        LogInfo("%s key = %s val = %s\n", __func__, entry->key, entry->value);

        sdl_mutex_lock(priv->sdl_mutex);
        if (0 == strcasecmp(entry->key, "low2mid_in_Hz")) {
            priv->bandwidth_low2mid =
                atoi(entry->value) * ACTUAL_LEN / SAMPLE_RATE_IN_HZ;
        } else if (0 == strcasecmp(entry->key, "mid2high_in_Hz")) {
            priv->bandwidth_mid2high =
                atoi(entry->value) * ACTUAL_LEN / SAMPLE_RATE_IN_HZ;
        } else if (0 == strcasecmp(entry->key, "all_band_gain_threshold")) {
            priv->gain_threshold_all = atof(entry->value);
        } else if (0 == strcasecmp(entry->key, "low_gain")) {
            priv->gain_threshold_low = atof(entry->value);
        } else if (0 == strcasecmp(entry->key, "mid_gain")) {
            priv->gain_threshold_mid = atof(entry->value);
        } else if (0 == strcasecmp(entry->key, "high_gain")) {
            priv->gain_threshold_high = atof(entry->value);
        } else if (0 == strcasecmp(entry->key, "mid_freq_gain")) {
            priv->mid_freq_gain = atof(entry->value);
        } else if (0 == strcasecmp(entry->key, "is_enhance_mid_freq")) {
            priv->is_enhance_mid_freq = atoi(entry->value);
        } else if (0 == strcasecmp(entry->key, "Switch")) {
            if (0 == strcasecmp(entry->value, "Off")) {
                priv->is_noise_suppression_on = false;
            } else if (0 == strcasecmp(entry->value, "On")) {
                priv->is_noise_suppression_on = true;
            }
        }
        sdl_mutex_unlock(priv->sdl_mutex);
    }
    return 0;
}

static int noise_suppression_send(EffectContext *ctx, const void *samples,
                                  const size_t nb_samples) {
    assert(NULL != ctx);
    priv_t *priv = (priv_t *)ctx->priv;
    assert(NULL != priv);
    assert(NULL != priv->fifo_in);

    if (priv->fifo_in) return fifo_write(priv->fifo_in, samples, nb_samples);
    return 0;
}

static void Windowing(priv_t *priv) {
    assert(NULL != priv);
    for (int i = 0; i < FFT_LEN; i += 4) {
        priv->fft_array[i] = (float)priv->raw_pcm[i] * HamTab_INV_MAX_15BITS[i];
        priv->fft_array[i + 1] =
            (float)priv->raw_pcm[i + 1] * HamTab_INV_MAX_15BITS[i + 1];
        priv->fft_array[i + 2] =
            (float)priv->raw_pcm[i + 2] * HamTab_INV_MAX_15BITS[i + 2];
        priv->fft_array[i + 3] =
            (float)priv->raw_pcm[i + 3] * HamTab_INV_MAX_15BITS[i + 3];
    }
}

static void WindowingHamTab(float *fft_array, float gain) {
    for (int i = 0; i < FFT_LEN; i += 4) {
        fft_array[i] *= gain * HamTab[i];
        fft_array[i + 1] *= gain * HamTab[i + 1];
        fft_array[i + 2] *= gain * HamTab[i + 2];
        fft_array[i + 3] *= gain * HamTab[i + 3];
    }
}

static void Energy(float *buffer, float *fft_array) {
    buffer[0] = fft_array[0] * fft_array[0];
    buffer[ACTUAL_LEN - 1] = fft_array[1] * fft_array[1];
    for (int i = 2, j = 1; i < ACTUAL_LEN - 1; i += 2, ++j) {
        buffer[j] =
            fft_array[i] * fft_array[i] + fft_array[i + 1] * fft_array[i + 1];
    }
}

static void SmssSmprDdSnrGain(priv_t *priv) {
    assert(NULL != priv);

    float *sig_prev = priv->noise_est->sig_prev;
    float *gain = priv->noise_est->gain;
    float *noise_ps = priv->noise_est->noise_ps;
    float *ns_ps = priv->ns_ps;

    float tmp1, tmp2, tmp3, num, denum, reproc, inv_sqrt;
    for (int i = 0; i < ACTUAL_LEN; ++i) {
        tmp1 = FFMAX(*ns_ps - *noise_ps, 0.0f);
        tmp2 = 0.03f * tmp1 + MIN_FLT;
        num = 0.97f * (*sig_prev) + tmp2;
        denum = THETA * (*noise_ps++) + num;
        reproc = Recip(denum);
        tmp3 = reproc * num;
        *sig_prev++ = tmp3 * (*ns_ps++);  // if VAD is on , modify "*sig_prev++"
                                          // of this line to "*sig_prev"
#ifdef VAD
        sum += *sig_prev++;
#endif
        inv_sqrt = RecipSqrt(tmp3);
        gain[i] = FFMAX(priv->gain_threshold_all, inv_sqrt * tmp3);
    }
    for (int i = 0; i < priv->bandwidth_low2mid; i++) {
        gain[i] = FFMIN(gain[i], priv->gain_threshold_low);
    }

    for (int i = priv->bandwidth_low2mid; i < priv->bandwidth_mid2high; i++) {
        gain[i] = FFMIN(gain[i], priv->gain_threshold_mid);
    }

    for (int i = priv->bandwidth_mid2high; i < ACTUAL_LEN; i++) {
        gain[i] = FFMIN(gain[i], priv->gain_threshold_high);
    }
    if (priv->is_enhance_mid_freq) {
        for (int i = 17; i < 35; i++) {
            gain[i] *= priv->mid_freq_gain;
        }
    }

#ifdef VAD
    sum *= INV_Actual_Len;
    tmp1 = Log10(sum, LogTable, Precision_N);
    tmp2 = noise_est_inst->log_eng;
    tmp3 = tmp1 - tmp2;
    noise_est_inst->log_eng = tmp3 * priv->noise_est->eta + tmp2;
    if (noise_est_inst->log_eng < noise_est_inst->eng_judge) {
        noise_est_inst->vad_floor = 0.1f;
    } else {
        noise_est_inst->vad_floor = 1.0f;
    }
#else
    priv->noise_est->vad_floor = 1.0f;
#endif
}

static void SmssSmprApplyGain(float *freq_src, float *gain) {
#ifdef DC_REDUCTION
    freq_src[0] = 0.0f;
    freq_src[1] *= gain[ACTUAL_LEN - 1];
    gain[1] = 0.0f;
    for (int i = 2, j = 1; i < FFT_LEN; i += 2, j++) {
        freq_src[i] *= gain[j];
        freq_src[i + 1] *= gain[j];
    }
#else
    freq_src[0] *= gain[0];
    freq_src[1] *= gain[ACTUAL_LEN - 1];

    for (int i = 2, j = 1; i < FFT_LEN; i += 2, j++) {
        freq_src[i] *= gain[j];
        freq_src[i + 1] *= gain[j];
    }
#endif
}

static void FrameOverlapAdd(priv_t *priv) {
    if (NULL == priv) return;
    float *flp_pcm = priv->flp_pcm;
    float *last_half_fft_array = priv->last_half_fft_array;
    float *fft_array = priv->fft_array;

    for (int i = 0; i < HALF_FFT_LEN; i += 4) {
        flp_pcm[i] = last_half_fft_array[i] + fft_array[i];
        flp_pcm[i + 1] = last_half_fft_array[i + 1] + fft_array[i + 1];
        flp_pcm[i + 2] = last_half_fft_array[i + 2] + fft_array[i + 2];
        flp_pcm[i + 3] = last_half_fft_array[i + 3] + fft_array[i + 3];
        last_half_fft_array[i] = fft_array[HALF_FFT_LEN + i];
        last_half_fft_array[i + 1] = fft_array[HALF_FFT_LEN + i + 1];
        last_half_fft_array[i + 2] = fft_array[HALF_FFT_LEN + i + 2];
        last_half_fft_array[i + 3] = fft_array[HALF_FFT_LEN + i + 3];
    }
}

static int noise_suppression_receive(EffectContext *ctx, void *samples,
                                     const size_t max_nb_samples) {
    assert(NULL != ctx);
    priv_t *priv = (priv_t *)ctx->priv;
    assert(NULL != priv);
    assert(NULL != priv->fifo_out);

    sdl_mutex_lock(priv->sdl_mutex);
    if (priv->is_noise_suppression_on) {
        while (fifo_occupancy(priv->fifo_in) >= FRAME_LEN) {
            memmove(priv->raw_pcm, priv->raw_pcm + FRAME_LEN,
                    FRAME_LEN * sizeof(int16_t));
            // 准备frame数据
            fifo_read(priv->fifo_in, priv->raw_pcm + FRAME_LEN, FRAME_LEN);
            // 加窗防止频谱泄露
            Windowing(priv);
            // 时域转频域
            ae_rdft_f(FFT_LEN, 1, priv->fft_array, priv->ip, priv->wfft);
            // 计算能量
            Energy(priv->ns_ps, priv->fft_array);
            // 噪声估计
            NoiseEstimationProcess(priv->noise_est, priv->ns_ps,
                                   &priv->is_first_frame);
            SmssSmprDdSnrGain(priv);
            SmssSmprApplyGain(priv->fft_array, priv->noise_est->gain);
            // 频域转时域
            ae_rdft_f(FFT_LEN, -1, priv->fft_array, priv->ip, priv->wfft);
            // 归一化重构后的原始信号
            WindowingHamTab(priv->fft_array,
                            FLP_INV_FFT_LEN * priv->noise_est->vad_floor);
            // 滤波信号输出
            FrameOverlapAdd(priv);
            // 浮点转定点
            FloatToS16(priv->flp_pcm, priv->fixed_pcm, FRAME_LEN);
            fifo_write(priv->fifo_out, priv->fixed_pcm, FRAME_LEN);
        }
    } else {
        while (fifo_occupancy(priv->fifo_in) > 0) {
            size_t nb_samples =
                fifo_read(priv->fifo_in, priv->fixed_pcm, FRAME_LEN);
            fifo_write(priv->fifo_out, priv->fixed_pcm, nb_samples);
        }
    }
    sdl_mutex_unlock(priv->sdl_mutex);

    if (atomic_load(&ctx->return_max_nb_samples) &&
        fifo_occupancy(priv->fifo_out) < max_nb_samples)
        return 0;
    // 读取原始数据
    return fifo_read(priv->fifo_out, samples, max_nb_samples);
}

const EffectHandler *effect_noise_suppression_fn(void) {
    static EffectHandler handler = {.name = "noise_suppression",
                                    .usage = "",
                                    .priv_size = sizeof(priv_t),
                                    .init = noise_suppression_init,
                                    .set = noise_suppression_set,
                                    .send = noise_suppression_send,
                                    .receive = noise_suppression_receive,
                                    .close = noise_suppression_close};
    return &handler;
}
