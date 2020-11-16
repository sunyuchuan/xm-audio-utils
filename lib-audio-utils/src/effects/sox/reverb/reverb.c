/* libSoX effect: stereo reverberation
 * Copyright (c) 2007 robs@users.sourceforge.net
 * Filter design based on freeverb by Jezar at Dreampoint.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "error_def.h"
#include "effect_struct.h"
#include "sox/sox.h"
#include "tools/fifo.h"
#include "tools/sdl_mutex.h"
#include "tools/conversion.h"

#define lsx_zalloc(var, n) var = calloc(n, sizeof(*var))
#define filter_advance(p) if (--(p)->ptr < (p)->buffer) (p)->ptr += (p)->size
#define filter_delete(p) free((p)->buffer)

#define MAX_BUFFER_SIZE 2048

#define FLOAT_SAMPLE_CLIP(d) \
  ((d) < 0.0f ? ((d) < -1.0f ? -1.0f : (d)) \
    : ((d) > 1.0f ? 1.0f: (d)))

typedef struct {
    size_t  size;
    float   * buffer, * ptr;
    float   store;
} filter_t;

static float comb_process(filter_t * p,  /* gcc -O2 will inline this */
                          float const * input, float const * feedback, float const * hf_damping)
{
    float output = *p->ptr;
    p->store = output + (p->store - output) * *hf_damping;
    *p->ptr = *input + p->store * *feedback;
    filter_advance(p);
    return output;
}

static float allpass_process(filter_t * p,  /* gcc -O2 will inline this */
                             float const * input)
{
    float output = *p->ptr;
    *p->ptr = *input + output * .5;
    filter_advance(p);
    return output - *input;
}

static const size_t /* Filter delay lengths in samples (44100Hz sample-rate) */
comb_lengths[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617},
                 allpass_lengths[] = {225, 341, 441, 556};
#define stereo_adjust 12

typedef struct {
    filter_t comb   [array_length(comb_lengths)];
    filter_t allpass[array_length(allpass_lengths)];
} filter_array_t;

static void filter_array_create(filter_array_t * p, double rate,
                                double scale, double offset)
{
    size_t i;
    double r = rate * (1 / 44100.); /* Compensate for actual sample-rate */

    for (i = 0; i < array_length(comb_lengths); ++i, offset = -offset) {
        filter_t * pcomb = &p->comb[i];
        pcomb->size = (size_t)(scale * r * (comb_lengths[i] + stereo_adjust * offset) + .5);
        pcomb->ptr = lsx_zalloc(pcomb->buffer, pcomb->size);
    }
    for (i = 0; i < array_length(allpass_lengths); ++i, offset = -offset) {
        filter_t * pallpass = &p->allpass[i];
        pallpass->size = (size_t)(r * (allpass_lengths[i] + stereo_adjust * offset) + .5);
        pallpass->ptr = lsx_zalloc(pallpass->buffer, pallpass->size);
    }
}

static void filter_array_process(filter_array_t * p,
                                 size_t length, float const * input, float * output,
                                 float const * feedback, float const * hf_damping, float const * gain)
{
    while (length--) {
        float out = 0, in = *input++;

        size_t i = array_length(comb_lengths) - 1;
        do out += comb_process(p->comb + i, &in, feedback, hf_damping);
        while (i--);

        i = array_length(allpass_lengths) - 1;
        do out = allpass_process(p->allpass + i, &out);
        while (i--);

        *output++ = out * *gain;
    }
}

static void filter_array_delete(filter_array_t * p)
{
    size_t i;

    for (i = 0; i < array_length(allpass_lengths); ++i)
        filter_delete(&p->allpass[i]);
    for (i = 0; i < array_length(comb_lengths); ++i)
        filter_delete(&p->comb[i]);
}

typedef struct {
    float feedback;
    float hf_damping;
    float gain;
    fifo *input_fifo;
    filter_array_t chan[2];
    float * out[2];
} reverb_t;

static void reverb_create(reverb_t * p, double sample_rate_Hz,
                          double wet_gain_dB,
                          double room_scale,     /* % */
                          double reverberance,   /* % */
                          double hf_damping,     /* % */
                          double pre_delay_ms,
                          double stereo_depth,
                          size_t buffer_size,
                          float * * out)
{
    size_t i, delay = pre_delay_ms / 1000 * sample_rate_Hz + .5;
    double scale = room_scale / 100 * .9 + .1;
    double depth = stereo_depth / 100;
    double a =  -1 /  log(1 - /**/.3 /**/);           /* Set minimum feedback */
    double b = 100 / (log(1 - /**/.98/**/) * a + 1);  /* Set maximum feedback */

    memset(p, 0, sizeof(*p));
    p->feedback = 1 - exp((reverberance - b) / (a * b));
    p->hf_damping = hf_damping / 100 * .3 + .2;
    p->gain = dB_to_linear(wet_gain_dB) * .015;
    p->input_fifo = fifo_create(sizeof(float));
    memset(fifo_reserve(p->input_fifo, delay), 0, delay * sizeof(float));
    for (i = 0; i <= ceil(depth); ++i) {
        filter_array_create(p->chan + i, sample_rate_Hz, scale, i * depth);
        out[i] = lsx_zalloc(p->out[i], buffer_size);
    }
}

static void reverb_process(reverb_t * p, size_t length)
{
    size_t i;
    for (i = 0; i < 2 && p->out[i]; ++i)
        filter_array_process(p->chan + i, length,
                             (float *) fifo_read_ptr(p->input_fifo),
                             p->out[i], &p->feedback, &p->hf_damping, &p->gain);
    fifo_update_ptr(p->input_fifo, length);
}

static void reverb_delete(reverb_t * p)
{
    size_t i;
    for (i = 0; i < 2 && p->out[i]; ++i) {
        free(p->out[i]);
        filter_array_delete(p->chan + i);
    }
    fifo_delete(&p->input_fifo);
}

/*------------------------------- SoX Wrapper --------------------------------*/

typedef struct {
    fifo *fifo_in;
    fifo *fifo_out;
    SdlMutex *sdl_mutex;
    bool effect_on;
    short *s_buf;
    float *in_buf;
    float *out_buf;

    double reverberance, hf_damping, pre_delay_ms;
    double stereo_depth, wet_gain_dB, room_scale;
    bool wet_only;

    size_t ichannels, ochannels;
    struct {
        reverb_t reverb;
        float * dry, * wet[2];
    } chan[2];
} priv_t;

static int getopts(EffectContext * effp, int argc, const char **argv)
{
    priv_t * p = (priv_t *)effp->priv;
    p->reverberance = p->hf_damping = 50; /* Set non-zero defaults */
    p->stereo_depth = p->room_scale = 100;

    --argc, ++argv;
    p->wet_only = argc && (!strcmp(*argv, "-w") || !strcmp(*argv, "--wet-only"))
                  && (--argc, ++argv, true);
    priv_t * priv = p;
    do {  /* break-able block */
        NUMERIC_PARAMETER(reverberance, 0, 100)
        NUMERIC_PARAMETER(hf_damping, 0, 100)
        NUMERIC_PARAMETER(room_scale, 0, 100)
        NUMERIC_PARAMETER(stereo_depth, 0, 100)
        NUMERIC_PARAMETER(pre_delay_ms, 0, 500)
        NUMERIC_PARAMETER(wet_gain_dB, -10, 10)
    } while (0);

    return argc ? AUDIO_EFFECT_EOF : AUDIO_EFFECT_SUCCESS;
}

static int start(EffectContext * effp)
{
    priv_t * p = (priv_t *)effp->priv;
    size_t i;

    p->ichannels = p->ochannels = effp->in_signal.channels;
    if (effp->in_signal.channels > 2 && p->stereo_depth) {
        LogWarning("stereo-depth not applicable with >2 channels");
        p->stereo_depth = 0;
    }

    if (effp->in_signal.channels == 1 && p->stereo_depth)
        return AUDIO_EFFECT_EOF;

    for (i = 0; i < p->ichannels; ++i)
        reverb_create(
            &p->chan[i].reverb, effp->in_signal.sample_rate,
            p->wet_gain_dB, p->room_scale, p->reverberance,
            p->hf_damping, p->pre_delay_ms, p->stereo_depth,
            MAX_BUFFER_SIZE / p->ochannels, p->chan[i].wet);

    if (effp->in_signal.mult)
        *effp->in_signal.mult /= !p->wet_only + 2 * dB_to_linear(max(0,p->wet_gain_dB));
    return AUDIO_EFFECT_SUCCESS;
}

static int flow(EffectContext * effp, const float * ibuf,
                float * obuf, size_t * isamp, size_t * osamp)
{
    priv_t * p = (priv_t *)effp->priv;
    size_t c, i, w, len = min(*isamp / p->ichannels, *osamp / p->ochannels);

    *isamp = len * p->ichannels, *osamp = len * p->ochannels;
    for (c = 0; c < p->ichannels; ++c)
        p->chan[c].dry = fifo_reserve(p->chan[c].reverb.input_fifo, len);
    for (i = 0; i < len; ++i) for (c = 0; c < p->ichannels; ++c) {
            p->chan[c].dry[i] = FLOAT_SAMPLE_CLIP(*ibuf);
            ibuf++;
        }
    for (c = 0; c < p->ichannels; ++c)
        reverb_process(&p->chan[c].reverb, len);
    if (p->ichannels == 2) for (i = 0; i < len; ++i) for (w = 0; w < 2; ++w) {
                float out = (1 - p->wet_only) * p->chan[w].dry[i] +
                            .5 * (p->chan[0].wet[w][i] + p->chan[1].wet[w][i]);
                *obuf++ = FLOAT_SAMPLE_CLIP(out);
            }
    else for (i = 0; i < len; ++i) for (w = 0; w < p->ochannels; ++w) {
                float out = (1 - p->wet_only) * p->chan[0].dry[i] + p->chan[0].wet[w][i];
                *obuf++ = FLOAT_SAMPLE_CLIP(out);
            }
    return AUDIO_EFFECT_SUCCESS;
}

static int stop(EffectContext * effp)
{
    priv_t * p = (priv_t *)effp->priv;
    size_t i;
    for (i = 0; i < p->ichannels; ++i)
        reverb_delete(&p->chan[i].reverb);
    return AUDIO_EFFECT_SUCCESS;
}

static int reverb_parseopts(EffectContext *ctx, const char *argv_s)
{
#define MAX_ARGC 50
    const char *argv[MAX_ARGC];
    int argc = 0;
    argv[argc++] = ctx->handler.name;

    char *_argv_s = calloc(strlen(argv_s) + 1, sizeof(char));
    memcpy(_argv_s, argv_s, strlen(argv_s) + 1);

    char *token = strtok(_argv_s, " ");
    while (token != NULL) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    int ret = getopts(ctx, argc, argv);
    if (ret < 0) goto end;
    ret = start(ctx);
    if (ret < 0) goto end;

end:
    free(_argv_s);
    return ret;
}

static int reverb_set_mode(EffectContext *ctx, const char *mode)
{
    LogInfo("%s mode = %s.\n", __func__, mode);
    priv_t *priv = (priv_t *)ctx->priv;
    if (0 == strcasecmp(mode, "None")) {
        return -1;
    } else {
        return reverb_parseopts(ctx, REVERB_PARAMS_SOX);
    }
}

static int reverb_close(EffectContext *ctx)
{
    LogInfo("%s.\n", __func__);
    if(NULL == ctx) return AEERROR_NULL_POINT;

    if (ctx->priv) {
        priv_t *priv = (priv_t *)ctx->priv;
        if (priv->fifo_in) fifo_delete(&priv->fifo_in);
        if (priv->fifo_out) fifo_delete(&priv->fifo_out);
        if (priv->sdl_mutex) sdl_mutex_free(&priv->sdl_mutex);
        if (priv->s_buf) {
            free(priv->s_buf);
            priv->s_buf = NULL;
        }
        if (priv->out_buf) {
            free(priv->out_buf);
            priv->out_buf = NULL;
        }
        if (priv->in_buf) {
            free(priv->in_buf);
            priv->in_buf = NULL;
        }
        stop(ctx);
    }
    return 0;
}

static int reverb_flush(EffectContext *ctx, void *samples,
                        const size_t max_nb_samples)
{
    if(!ctx || !samples) return AEERROR_NULL_POINT;
    priv_t *priv = (priv_t *)ctx->priv;
    if (!priv || !priv->fifo_out)
        return AEERROR_NULL_POINT;

    return fifo_read(priv->fifo_out, samples, max_nb_samples);
}

static int reverb_receive(EffectContext *ctx, void *samples,
                          const size_t max_nb_samples)
{
    if(!ctx || !samples) return AEERROR_NULL_POINT;
    priv_t *priv = (priv_t *)ctx->priv;
    if (!priv || !priv->fifo_in
        || !priv->fifo_out || !priv->in_buf
        || !priv->out_buf || !priv->s_buf) return AEERROR_NULL_POINT;

    sdl_mutex_lock(priv->sdl_mutex);
    if (priv->effect_on) {
        size_t nb_samples =
            fifo_read(priv->fifo_in, priv->s_buf, MAX_BUFFER_SIZE);
        S16ToFloat(priv->s_buf, priv->in_buf, nb_samples);
        while (nb_samples > 0) {
            size_t in_len = nb_samples;
            size_t out_len = nb_samples;
            flow(ctx, priv->in_buf, priv->out_buf, &in_len, &out_len);
            FloatToS16(priv->out_buf, priv->s_buf, out_len);
            fifo_write(priv->fifo_out, priv->s_buf, out_len);
            nb_samples =
                fifo_read(priv->fifo_in, priv->s_buf, MAX_BUFFER_SIZE);
            S16ToFloat(priv->s_buf, priv->in_buf, nb_samples);
        }
    } else {
        while (fifo_occupancy(priv->fifo_in) > 0) {
            size_t nb_samples =
                fifo_read(priv->fifo_in, priv->s_buf, MAX_BUFFER_SIZE);
            fifo_write(priv->fifo_out, priv->s_buf, nb_samples);
        }
    }
    sdl_mutex_unlock(priv->sdl_mutex);

    if (atomic_load(&ctx->return_max_nb_samples) &&
        fifo_occupancy(priv->fifo_out) < max_nb_samples)
        return 0;

    return fifo_read(priv->fifo_out, samples, max_nb_samples);
}

static int reverb_send(EffectContext *ctx, const void *samples,
                       const size_t nb_samples)
{
    if(!ctx || !samples || nb_samples <= 0) return AEERROR_NULL_POINT;
    priv_t *priv = (priv_t *)ctx->priv;
    if (!priv || !priv->fifo_in) return AEERROR_NULL_POINT;

    return fifo_write(priv->fifo_in, samples, nb_samples);
}

static int reverb_set(EffectContext *ctx, const char *key, int flags)
{
    LogInfo("%s.\n", __func__);
    if(NULL == ctx || NULL == key) return AEERROR_NULL_POINT;

    priv_t *priv = (priv_t *)ctx->priv;
    if (NULL == priv) return AEERROR_NULL_POINT;

    int ret = 0;
    AEDictionaryEntry *entry = ae_dict_get(ctx->options, key, NULL, flags);
    if (entry) {
        LogInfo("%s key = %s val = %s\n", __func__, entry->key, entry->value);

        sdl_mutex_lock(priv->sdl_mutex);
        if (0 == strcasecmp(entry->key, ctx->handler.name)) {
            ret = reverb_parseopts(ctx, entry->value);
            if (ret >= 0) priv->effect_on = true;
            else priv->effect_on = false;
        } else if (0 == strcasecmp(entry->key, "mode")) {
            ret = reverb_set_mode(ctx, entry->value);
            if (ret >= 0) priv->effect_on = true;
            else priv->effect_on = false;
        }
        sdl_mutex_unlock(priv->sdl_mutex);
    }
    return ret;
}

static int reverb_init(EffectContext *ctx, int argc, const char **argv)
{
    LogInfo("%s.\n", __func__);
    if(NULL == ctx) return AEERROR_NULL_POINT;

    priv_t *priv = (priv_t *)ctx->priv;
    if (NULL == priv) return AEERROR_NULL_POINT;

    int ret = 0;
    priv->fifo_in = fifo_create(sizeof(sample_type));
    if (NULL == priv->fifo_in) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    priv->fifo_out = fifo_create(sizeof(sample_type));
    if (NULL == priv->fifo_out) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    priv->sdl_mutex = sdl_mutex_create();
    if (NULL == priv->sdl_mutex) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    priv->s_buf = calloc((size_t)MAX_BUFFER_SIZE, sizeof(*priv->s_buf));
    if (NULL == priv->s_buf) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    priv->in_buf = calloc((size_t)MAX_BUFFER_SIZE, sizeof(*priv->in_buf));
    if (NULL == priv->in_buf) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    priv->out_buf = calloc((size_t)MAX_BUFFER_SIZE, sizeof(*priv->out_buf));
    if (NULL == priv->out_buf) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    priv->effect_on = false;

    if (argc > 1 && argv != NULL) {
        ret = getopts(ctx, argc, argv);
        if (ret < 0) goto end;
        ret = start(ctx);
        if (ret < 0) goto end;
    }

    return ret;
end:
    if (ret < 0) reverb_close(ctx);
    return ret;
}

const EffectHandler *effect_reverb_sox_fn(void)
{
    static EffectHandler handler = {
        .name = "reverb_sox",
        .usage =
        "[-w|--wet-only]"
        " [reverberance (50%)"
        " [HF-damping (50%)"
        " [room-scale (100%)"
        " [stereo-depth (100%)"
        " [pre-delay (0ms)"
        " [wet-gain (0dB)"
        "]]]]]]",
        .priv_size = sizeof(priv_t),
        .init = reverb_init,
        .set = reverb_set,
        .send = reverb_send,
        .receive = reverb_receive,
        .flush = reverb_flush,
        .close = reverb_close
    };
    return &handler;
}
