/* libSoX Biquad filter effects   (c) 2006-8 robs@users.sourceforge.net
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
 *
 *
 * 2-pole filters designed by Robert Bristow-Johnson <rbj@audioimagination.com>
 *   see http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
 *
 * 1-pole filters based on code (c) 2000 Chris Bagwell <cbagwell@sprynet.com>
 *   Algorithms: Recursive single pole low/high pass filter
 *   Reference: The Scientist and Engineer's Guide to Digital Signal Processing
 *
 *   low-pass: output[N] = input[N] * A + output[N-1] * B
 *     X = exp(-2.0 * pi * Fc)
 *     A = 1 - X
 *     B = X
 *     Fc = cutoff freq / sample rate
 *
 *     Mimics an RC low-pass filter:
 *
 *     ---/\/\/\/\----------->
 *                   |
 *                  --- C
 *                  ---
 *                   |
 *                   |
 *                   V
 *
 *   high-pass: output[N] = A0 * input[N] + A1 * input[N-1] + B1 * output[N-1]
 *     X  = exp(-2.0 * pi * Fc)
 *     A0 = (1 + X) / 2
 *     A1 = -(1 + X) / 2
 *     B1 = X
 *     Fc = cutoff freq / sample rate
 *
 *     Mimics an RC high-pass filter:
 *
 *         || C
 *     ----||--------->
 *         ||    |
 *               <
 *               > R
 *               <
 *               |
 *               V
 */


#include "biquad.h"
#include <assert.h>
#include <string.h>

typedef biquad_t priv_t;

static int hilo1_getopts(EffectContext * ctx, int argc, const char **argv) {
  return lsx_biquad_getopts(ctx, argc, argv, 1, 1, 0, 1, 2, "",
      *ctx->handler.name == 'l'? filter_LPF_1 : filter_HPF_1);
}


static int hilo2_getopts(EffectContext * ctx, int argc, const char **argv) {
  priv_t * p = (priv_t *)ctx->priv;
  if (argc > 1 && strcmp(argv[1], "-1") == 0)
    return hilo1_getopts(ctx, argc - 1, argv + 1);
  if (argc > 1 && strcmp(argv[1], "-2") == 0)
    ++argv, --argc;
  p->width = sqrt(0.5); /* Default to Butterworth */
  return lsx_biquad_getopts(ctx, argc, argv, 1, 2, 0, 1, 2, "qohk",
      *ctx->handler.name == 'l'? filter_LPF : filter_HPF);
}


static int bandpass_getopts(EffectContext * ctx, int argc, const char **argv) {
  filter_t type = filter_BPF;
  if (argc > 1 && strcmp(argv[1], "-c") == 0)
    ++argv, --argc, type = filter_BPF_CSG;
  return lsx_biquad_getopts(ctx, argc, argv, 2, 2, 0, 1, 2, "hkqob", type);
}


static int bandrej_getopts(EffectContext * ctx, int argc, const char **argv) {
  return lsx_biquad_getopts(ctx, argc, argv, 2, 2, 0, 1, 2, "hkqob", filter_notch);
}


static int allpass_getopts(EffectContext * ctx, int argc, const char **argv) {
  filter_t type = filter_APF;
  int m;
  if (argc > 1 && strcmp(argv[1], "-1") == 0)
    ++argv, --argc, type = filter_AP1;
  else if (argc > 1 && strcmp(argv[1], "-2") == 0)
    ++argv, --argc, type = filter_AP2;
  m = 1 + (type == filter_APF);
  return lsx_biquad_getopts(ctx, argc, argv, m, m, 0, 1, 2, "hkqo", type);
}


static int tone_getopts(EffectContext * ctx, int argc, const char **argv) {
  priv_t * p = (priv_t *)ctx->priv;
  p->width = 0.5;
  p->fc = *ctx->handler.name == 'b'? 100 : 3000;
  return lsx_biquad_getopts(ctx, argc, argv, 1, 3, 1, 2, 0, "shkqo",
      *ctx->handler.name == 'b'?  filter_lowShelf: filter_highShelf);
}


static int equalizer_getopts(EffectContext * ctx, int argc, const char **argv) {
  return lsx_biquad_getopts(ctx, argc, argv, 3, 3, 0, 1, 2, "qohk", filter_peakingEQ);
}

static int band_getopts(EffectContext * ctx, int argc, const char **argv)
{
  filter_t type = filter_BPF_SPK;
  if (argc > 1 && strcmp(argv[1], "-n") == 0)
    ++argv, --argc, type = filter_BPF_SPK_N;
  return lsx_biquad_getopts(ctx, argc, argv, 1, 2, 0, 1, 2, "hkqo", type);
}

static int deemph_getopts(EffectContext * ctx, int argc, const char **argv) {
  return lsx_biquad_getopts(ctx, argc, argv, 0, 0, 0, 1, 2, "s", filter_deemph);
}


static int riaa_getopts(EffectContext * ctx, int argc, const char **argv) {
  priv_t * p = (priv_t *)ctx->priv;
  p->filter_type = filter_riaa;
  (void)argv;
  return --argc? AUDIO_EFFECT_EOF : AUDIO_EFFECT_SUCCESS;
}


static int biquad_getopts(EffectContext * ctx, int argc, const char **argv) {
  return create(ctx, argc, argv);
}


static void make_poly_from_roots(
    double const * roots, size_t num_roots, double * poly)
{
  size_t i, j;
  poly[0] = 1;
  poly[1] = -roots[0];
  memset(poly + 2, 0, (num_roots + 1 - 2) * sizeof(*poly));
  for (i = 1; i < num_roots; ++i)
    for (j = num_roots; j > 0; --j)
      poly[j] -= poly[j - 1] * roots[i];
}

static int start(EffectContext * ctx)
{
  if (0 == strcasecmp(ctx->handler.name, "biquad")) {
    return lsx_biquad_start(ctx);
  }

  priv_t * p = (priv_t *)ctx->priv;
  double w0, A, alpha, mult;

  if (p->filter_type == filter_deemph) { /* See deemph.plt for documentation */
    if (ctx->in_signal.sample_rate == 44100) {
      p->fc    = 5283;
      p->width = 0.4845;
      p->gain  = -9.477;
    }
    else if (ctx->in_signal.sample_rate == 48000) {
      p->fc    = 5356;
      p->width = 0.479;
      p->gain  = -9.62;
    }
    else {
      LogError("sample rate must be 44100 (audio-CD) or 48000 (DAT)");
      return AUDIO_EFFECT_EOF;
    }
  }

  w0 = 2 * M_PI * p->fc / ctx->in_signal.sample_rate;
  A  = exp(p->gain / 40 * log(10.));
  alpha = 0, mult = dB_to_linear(max(p->gain, 0));

  if (w0 > M_PI) {
    LogError("frequency must be less than half the sample-rate (Nyquist rate)");
    return AUDIO_EFFECT_EOF;
  }

  /* Set defaults: */
  p->b0 = p->b1 = p->b2 = p->a1 = p->a2 = 0;
  p->a0 = 1;

  if (p->width) switch (p->width_type) {
    case width_slope:
      alpha = sin(w0)/2 * sqrt((A + 1/A)*(1/p->width - 1) + 2);
      break;

    case width_Q:
      alpha = sin(w0)/(2*p->width);
      break;

    case width_bw_oct:
      alpha = sin(w0)*sinh(log(2.)/2 * p->width * w0/sin(w0));
      break;

    case width_bw_Hz:
      alpha = sin(w0)/(2*p->fc/p->width);
      break;

    case width_bw_kHz: assert(0); /* Shouldn't get here */

    case width_bw_old:
      alpha = tan(M_PI * p->width / ctx->in_signal.sample_rate);
      break;
  }
  switch (p->filter_type) {
    case filter_LPF: /* H(s) = 1 / (s^2 + s/Q + 1) */
      p->b0 =  (1 - cos(w0))/2;
      p->b1 =   1 - cos(w0);
      p->b2 =  (1 - cos(w0))/2;
      p->a0 =   1 + alpha;
      p->a1 =  -2*cos(w0);
      p->a2 =   1 - alpha;
      break;

    case filter_HPF: /* H(s) = s^2 / (s^2 + s/Q + 1) */
      p->b0 =  (1 + cos(w0))/2;
      p->b1 = -(1 + cos(w0));
      p->b2 =  (1 + cos(w0))/2;
      p->a0 =   1 + alpha;
      p->a1 =  -2*cos(w0);
      p->a2 =   1 - alpha;
      break;

    case filter_BPF_CSG: /* H(s) = s / (s^2 + s/Q + 1)  (constant skirt gain, peak gain = Q) */
      p->b0 =   sin(w0)/2;
      p->b1 =   0;
      p->b2 =  -sin(w0)/2;
      p->a0 =   1 + alpha;
      p->a1 =  -2*cos(w0);
      p->a2 =   1 - alpha;
      break;

    case filter_BPF: /* H(s) = (s/Q) / (s^2 + s/Q + 1)      (constant 0 dB peak gain) */
      p->b0 =   alpha;
      p->b1 =   0;
      p->b2 =  -alpha;
      p->a0 =   1 + alpha;
      p->a1 =  -2*cos(w0);
      p->a2 =   1 - alpha;
      break;

    case filter_notch: /* H(s) = (s^2 + 1) / (s^2 + s/Q + 1) */
      p->b0 =   1;
      p->b1 =  -2*cos(w0);
      p->b2 =   1;
      p->a0 =   1 + alpha;
      p->a1 =  -2*cos(w0);
      p->a2 =   1 - alpha;
      break;

    case filter_APF: /* H(s) = (s^2 - s/Q + 1) / (s^2 + s/Q + 1) */
      p->b0 =   1 - alpha;
      p->b1 =  -2*cos(w0);
      p->b2 =   1 + alpha;
      p->a0 =   1 + alpha;
      p->a1 =  -2*cos(w0);
      p->a2 =   1 - alpha;
      break;

    case filter_peakingEQ: /* H(s) = (s^2 + s*(A/Q) + 1) / (s^2 + s/(A*Q) + 1) */
      if (A == 1)
        return AUDIO_EFFECT_EOF;
      p->b0 =   1 + alpha*A;
      p->b1 =  -2*cos(w0);
      p->b2 =   1 - alpha*A;
      p->a0 =   1 + alpha/A;
      p->a1 =  -2*cos(w0);
      p->a2 =   1 - alpha/A;
      break;

    case filter_lowShelf: /* H(s) = A * (s^2 + (sqrt(A)/Q)*s + A)/(A*s^2 + (sqrt(A)/Q)*s + 1) */
      if (A == 1)
        return AUDIO_EFFECT_EOF;
      p->b0 =    A*( (A+1) - (A-1)*cos(w0) + 2*sqrt(A)*alpha );
      p->b1 =  2*A*( (A-1) - (A+1)*cos(w0)                   );
      p->b2 =    A*( (A+1) - (A-1)*cos(w0) - 2*sqrt(A)*alpha );
      p->a0 =        (A+1) + (A-1)*cos(w0) + 2*sqrt(A)*alpha;
      p->a1 =   -2*( (A-1) + (A+1)*cos(w0)                   );
      p->a2 =        (A+1) + (A-1)*cos(w0) - 2*sqrt(A)*alpha;
      break;

    case filter_deemph: /* Falls through to high-shelf... */

    case filter_highShelf: /* H(s) = A * (A*s^2 + (sqrt(A)/Q)*s + 1)/(s^2 + (sqrt(A)/Q)*s + A) */
      if (!A)
        return AUDIO_EFFECT_EOF;
      p->b0 =    A*( (A+1) + (A-1)*cos(w0) + 2*sqrt(A)*alpha );
      p->b1 = -2*A*( (A-1) + (A+1)*cos(w0)                   );
      p->b2 =    A*( (A+1) + (A-1)*cos(w0) - 2*sqrt(A)*alpha );
      p->a0 =        (A+1) - (A-1)*cos(w0) + 2*sqrt(A)*alpha;
      p->a1 =    2*( (A-1) - (A+1)*cos(w0)                   );
      p->a2 =        (A+1) - (A-1)*cos(w0) - 2*sqrt(A)*alpha;
      break;

    case filter_LPF_1: /* single-pole */
      p->a1 = -exp(-w0);
      p->b0 = 1 + p->a1;
      break;

    case filter_HPF_1: /* single-pole */
      p->a1 = -exp(-w0);
      p->b0 = (1 - p->a1)/2;
      p->b1 = -p->b0;
      break;

    case filter_BPF_SPK: case filter_BPF_SPK_N: {
      double bw_Hz;
      if (!p->width)
        p->width = p->fc / 2;
      bw_Hz = p->width_type == width_Q?  p->fc / p->width :
        p->width_type == width_bw_Hz? p->width :
        p->fc * (pow(2., p->width) - 1) * pow(2., -0.5 * p->width); /* bw_oct */
      #include "band.h" /* Has different licence */
      break;
    }

    case filter_AP1:     /* Experimental 1-pole all-pass from Tom Erbe @ UCSD */
      p->b0 = exp(-w0);
      p->b1 = -1;
      p->a1 = -exp(-w0);
      break;

    case filter_AP2:     /* Experimental 2-pole all-pass from Tom Erbe @ UCSD */
      p->b0 = 1 - sin(w0);
      p->b1 = -2 * cos(w0);
      p->b2 = 1 + sin(w0);
      p->a0 = 1 + sin(w0);
      p->a1 = -2 * cos(w0);
      p->a2 = 1 - sin(w0);
      break;

    case filter_riaa: /* http://www.dsprelated.com/showmessage/73300/3.php */
      if (ctx->in_signal.sample_rate == 44100) {
        static const double zeros[] = {-0.2014898, 0.9233820};
        static const double poles[] = {0.7083149, 0.9924091};
        make_poly_from_roots(zeros, (size_t)2, &p->b0);
        make_poly_from_roots(poles, (size_t)2, &p->a0);
      }
      else if (ctx->in_signal.sample_rate == 48000) {
        static const double zeros[] = {-0.1766069, 0.9321590};
        static const double poles[] = {0.7396325, 0.9931330};
        make_poly_from_roots(zeros, (size_t)2, &p->b0);
        make_poly_from_roots(poles, (size_t)2, &p->a0);
      }
      else if (ctx->in_signal.sample_rate == 88200) {
        static const double zeros[] = {-0.1168735, 0.9648312};
        static const double poles[] = {0.8590646, 0.9964002};
        make_poly_from_roots(zeros, (size_t)2, &p->b0);
        make_poly_from_roots(poles, (size_t)2, &p->a0);
      }
      else if (ctx->in_signal.sample_rate == 96000) {
        static const double zeros[] = {-0.1141486, 0.9676817};
        static const double poles[] = {0.8699137, 0.9966946};
        make_poly_from_roots(zeros, (size_t)2, &p->b0);
        make_poly_from_roots(poles, (size_t)2, &p->a0);
      }
      else {
        LogError("Sample rate must be 44.1k, 48k, 88.2k, or 96k");
        return AUDIO_EFFECT_EOF;
      }
      { /* Normalise to 0dB at 1kHz (Thanks to Glenn Davis) */
        double y = 2 * M_PI * 1000 / ctx->in_signal.sample_rate;
        double b_re = p->b0 + p->b1 * cos(-y) + p->b2 * cos(-2 * y);
        double a_re = p->a0 + p->a1 * cos(-y) + p->a2 * cos(-2 * y);
        double b_im = p->b1 * sin(-y) + p->b2 * sin(-2 * y);
        double a_im = p->a1 * sin(-y) + p->a2 * sin(-2 * y);
        double g = 1 / sqrt((sqr(b_re) + sqr(b_im)) / (sqr(a_re) + sqr(a_im)));
        p->b0 *= g; p->b1 *= g; p->b2 *= g;
      }
      mult = (p->b0 + p->b1 + p->b2) / (p->a0 + p->a1 + p->a2);
      LogDebug("gain=%f", linear_to_dB(mult));
      break;
  }
  if (ctx->in_signal.mult)
    *ctx->in_signal.mult /= mult;
  return lsx_biquad_start(ctx);
}

static int flow(EffectContext *ctx,
  const sample_type *ibuf, sample_type *obuf,
  size_t *isamp, size_t *osamp)
{
  return lsx_biquad_flow(ctx, ibuf, obuf, isamp, osamp);
}

typedef struct name_group_t {
  const char *name;
  int (*getopts)(EffectContext * ctx, int argc, const char **argv);
} name_group_t;

static const name_group_t g_groups[] = {
  {"highpass", hilo2_getopts},
  {"lowpass", hilo2_getopts},
  {"bandpass", bandpass_getopts},
  {"bandreject", bandrej_getopts},
  {"allpass", allpass_getopts},
  {"bass", tone_getopts},
  {"treble", tone_getopts},
  {"equalizer", equalizer_getopts},
  {"band", band_getopts},
  {"deemph", deemph_getopts},
  {"riaa", riaa_getopts},
  {"biquad", biquad_getopts}
};

static int getopts(EffectContext *ctx, int argc, const char **argv) {
    if (!ctx) return AUDIO_EFFECT_EOF;

    for (size_t i = 0;
            i < sizeof(g_groups) / sizeof(name_group_t); ++i) {
        if (0 == strcasecmp(ctx->handler.name, g_groups[i].name)) {
            return (g_groups[i].getopts)(ctx, argc, argv);
        }
    }
    return AUDIO_EFFECT_EOF;
}

static int biquads_parseopts(EffectContext *ctx, const char *argv_s) {
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

static int biquads_set_mode(EffectContext *ctx, const char *mode) {
    LogInfo("%s mode = %s.\n", __func__, mode);
    priv_t *priv = (priv_t *)ctx->priv;
    if (0 == strcasecmp(mode, "None")) {
        return -1;
    } else {
        return biquads_parseopts(ctx, BIQUADS_PARAMS);
    }
}

static int biquads_close(EffectContext *ctx) {
    LogInfo("%s.\n", __func__);
    if(NULL == ctx) return AEERROR_NULL_POINT;

    if (ctx->priv) {
        priv_t *priv = (priv_t *)ctx->priv;
        if (priv->fifo_in) fifo_delete(&priv->fifo_in);
        if (priv->fifo_out) fifo_delete(&priv->fifo_out);
        if (priv->sdl_mutex) sdl_mutex_free(&priv->sdl_mutex);
        if (priv->in_buf) {
            free(priv->in_buf);
            priv->in_buf = NULL;
        }
        if (priv->out_buf) {
            free(priv->out_buf);
            priv->out_buf = NULL;
        }
    }
    return 0;
}

static int biquads_flush(EffectContext *ctx, void *samples,
                          const size_t max_nb_samples) {
    if(!ctx || !samples) return AEERROR_NULL_POINT;
    priv_t *priv = (priv_t *)ctx->priv;
    if (!priv || !priv->fifo_out)
        return AEERROR_NULL_POINT;

    return fifo_read(priv->fifo_out, samples, max_nb_samples);
}

static int biquads_receive(EffectContext *ctx, void *samples,
                          const size_t max_nb_samples) {
    if(!ctx || !samples) return AEERROR_NULL_POINT;
    priv_t *priv = (priv_t *)ctx->priv;
    if (!priv || !priv->fifo_in
        || !priv->fifo_out || !priv->in_buf
        || !priv->out_buf) return AEERROR_NULL_POINT;

    sdl_mutex_lock(priv->sdl_mutex);
    if (priv->effect_on) {
        size_t nb_samples =
            fifo_read(priv->fifo_in, priv->in_buf, MAX_SAMPLE_SIZE);
        while (nb_samples > 0) {
            size_t in_len = nb_samples;
            size_t out_len = nb_samples;
            flow(ctx, priv->in_buf, priv->out_buf, &in_len, &out_len);
            fifo_write(priv->fifo_out, priv->out_buf, out_len);
            nb_samples =
                fifo_read(priv->fifo_in, priv->in_buf, MAX_SAMPLE_SIZE);
        }
    } else {
        while (fifo_occupancy(priv->fifo_in) > 0) {
            size_t nb_samples =
                fifo_read(priv->fifo_in, priv->out_buf, max_nb_samples);
            fifo_write(priv->fifo_out, priv->out_buf, nb_samples);
        }
    }
    sdl_mutex_unlock(priv->sdl_mutex);

    if (atomic_load(&ctx->return_max_nb_samples) &&
        fifo_occupancy(priv->fifo_out) < max_nb_samples)
        return 0;

    return fifo_read(priv->fifo_out, samples, max_nb_samples);
}

static int biquads_send(EffectContext *ctx, const void *samples,
                      const size_t nb_samples) {
    if(!ctx || !samples || nb_samples <= 0) return AEERROR_NULL_POINT;
    priv_t *priv = (priv_t *)ctx->priv;
    if (!priv || !priv->fifo_in) return AEERROR_NULL_POINT;

    return fifo_write(priv->fifo_in, samples, nb_samples);
}

static int biquads_set(EffectContext *ctx, const char *key, int flags) {
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
            ret = biquads_parseopts(ctx, entry->value);
            if (ret >= 0) priv->effect_on = true;
            else priv->effect_on = false;
        } else if (0 == strcasecmp(entry->key, "mode")) {
            ret = biquads_set_mode(ctx, entry->value);
            if (ret >= 0) priv->effect_on = true;
            else priv->effect_on = false;
        }
        sdl_mutex_unlock(priv->sdl_mutex);
    }
    return ret;
}

static int biquads_init(EffectContext *ctx, int argc, const char **argv) {
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
    priv->in_buf = calloc((size_t)MAX_SAMPLE_SIZE, sizeof(*priv->in_buf));
    if (NULL == priv->in_buf) {
        ret = AEERROR_NOMEM;
        goto end;
    }
    priv->out_buf = calloc((size_t)MAX_SAMPLE_SIZE, sizeof(*priv->out_buf));
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
    if (ret < 0) biquads_close(ctx);
    return ret;
}

#define BIQUAD_EFFECT(name,usage) \
const EffectHandler *effect_##name##_fn(void) { \
  static EffectHandler handler = { \
    #name, usage, sizeof(biquad_t),\
    biquads_init, biquads_set, biquads_send, biquads_receive, biquads_flush, biquads_close \
  }; \
  return &handler; \
}

BIQUAD_EFFECT(highpass,    "[-1|-2] frequency [width[q|o|h|k](0.707q)]")
BIQUAD_EFFECT(lowpass,    "[-1|-2] frequency [width[q|o|h|k]](0.707q)")
BIQUAD_EFFECT(bandpass, "[-c] frequency width[h|k|q|o]")
BIQUAD_EFFECT(bandreject,  "frequency width[h|k|q|o]")
BIQUAD_EFFECT(allpass,  "frequency width[h|k|q|o]")
BIQUAD_EFFECT(bass,     "gain [frequency(100) [width[s|h|k|q|o]](0.5s)]")
BIQUAD_EFFECT(treble,     "gain [frequency(3000) [width[s|h|k|q|o]](0.5s)]")
BIQUAD_EFFECT(equalizer,"frequency width[q|o|h|k] gain")
BIQUAD_EFFECT(band,     "[-n] center [width[h|k|q|o]]")
BIQUAD_EFFECT(deemph,   NULL)
BIQUAD_EFFECT(riaa,     NULL)
BIQUAD_EFFECT(biquad, "b0 b1 b2 a0 a1 a2")
