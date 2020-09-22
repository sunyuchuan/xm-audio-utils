/* multiband compander effect for SoX
 * by Daniel Pouzzner <douzzer@mega.nu> 2002-Oct-8
 *
 * Compander code adapted from the SoX compand effect, by Nick Bailey
 *
 * SoX is Copyright 1999 Chris Bagwell And Nick Bailey This source code is
 * freely redistributable and may be used for any purpose.  This copyright
 * notice must be maintained.  Chris Bagwell And Nick Bailey are not
 * responsible for the consequences of using this software.
 *
 *
 * Usage:
 *   mcompand quoted_compand_args [crossover_frequency
 *      quoted_compand_args [...]]
 *
 *   quoted_compand_args are as for the compand effect:
 *
 *   attack1,decay1[,attack2,decay2...]
 *                  in-dB1,out-dB1[,in-dB2,out-dB2...]
 *                 [ gain [ initial-volume [ delay ] ] ]
 *
 *   Beware a variety of headroom (clipping) bugaboos.
 *
 * Implementation details:
 *   The input is divided into bands using 4th order Linkwitz-Riley IIRs.
 *   This is akin to the crossover of a loudspeaker, and results in flat
 *   frequency response absent compander action.
 *
 *   The outputs of the array of companders is summed, and sample truncation
 *   is done on the final sum.
 *
 *   Modifications to the predictive compression code properly maintain
 *   alignment of the outputs of the array of companders when the companders
 *   have different prediction intervals (volume application delays).  Note
 *   that the predictive mode of the limiter needs some TLC - in fact, a
 *   rewrite - since what's really useful is to assure that a waveform won't
 *   be clipped, by slewing the volume in advance so that the peak is at
 *   limit (or below, if there's a higher subsequent peak visible in the
 *   lookahead window) once it's reached.  */

#ifdef NDEBUG /* Enable assert always. */
#undef NDEBUG /* Must undef above assert.h or other that might include it. */
#endif

#include <assert.h>
#include "sox/compand/compandt.h"
#include "mcompand_xover.h"
#include "libavutil/mem.h"
#include "tools/fifo.h"
#include "tools/sdl_mutex.h"

#define mcompand_usage \
    "quoted_compand_args [crossover_frequency[k] quoted_compand_args [...]]\n" \
    "\n" \
    "quoted_compand_args are as for the compand effect:\n" \
    "\n" \
    "  attack1,decay1[,attack2,decay2...]\n" \
    "                 in-dB1,out-dB1[,in-dB2,out-dB2...]\n" \
    "                [ gain [ initial-volume [ delay ] ] ]"

typedef struct {
  sox_compandt_t transfer_fn;

  size_t expectedChannels; /* Also flags that channels aren't to be treated
                           individually when = 1 and input not mono */
  double *attackRate;   /* An array of attack rates */
  double *decayRate;    /*    ... and of decay rates */
  double *volume;       /* Current "volume" of each channel */
  double delay;         /* Delay to apply before companding */
  double topfreq;       /* upper bound crossover frequency */
  crossover_t filter;
  sample_type *delay_buf;   /* Old samples, used for delay processing */
  size_t delay_size;    /* lookahead for this band (in samples) - function of delay, above */
  ptrdiff_t delay_buf_ptr; /* Index into delay_buf */
  size_t delay_buf_cnt; /* No. of active entries in delay_buf */
} comp_band_t;

typedef struct {
  fifo *fifo_in;
  fifo *fifo_out;
  SdlMutex *sdl_mutex;
  bool effect_on;
  sample_type *in_buf;
  sample_type *out_buf;
  size_t nBands;
  sample_type *band_buf1, *band_buf2, *band_buf3;
  size_t band_buf_len;
  size_t delay_buf_size;/* Size of delay_buf in samples */
  comp_band_t *bands;

  char *arg; /* copy of current argument */
} priv_t;

/*
 * Process options
 *
 * Don't do initialization now.
 * The 'info' fields are not yet filled in.
 */
static int sox_mcompand_getopts_1(comp_band_t * l, size_t n, char **argv)
{
      char *s;
      size_t rates, i, commas;

      /* Start by checking the attack and decay rates */

      for (s = argv[0], commas = 0; *s; ++s)
        if (*s == ',') ++commas;

      if (commas % 2 == 0) /* There must be an even number of
                              attack/decay parameters */
      {
        LogError("compander: Odd number of attack & decay rate parameters.\n");
        return (AUDIO_EFFECT_EOF);
      }

      rates = 1 + commas/2;
      l->attackRate = malloc(sizeof(double) * rates);
      l->decayRate  = malloc(sizeof(double) * rates);
      l->volume = malloc(sizeof(double) * rates);
      l->expectedChannels = rates;
      l->delay_buf = NULL;

      /* Now tokenise the rates string and set up these arrays.  Keep
         them in seconds at the moment: we don't know the sample rate yet. */

      s = strtok(argv[0], ","); i = 0;
      do {
        l->attackRate[i] = atof(s); s = strtok(NULL, ",");
        l->decayRate[i]  = atof(s); s = strtok(NULL, ",");
        ++i;
      } while (s != NULL);

      if (!lsx_compandt_parse(&l->transfer_fn, argv[1], n>2 ? argv[2] : 0))
        return AUDIO_EFFECT_EOF;

      /* Set the initial "volume" to be attibuted to the input channels.
         Unless specified, choose 1.0 (maximum) otherwise clipping will
         result if the user has seleced a long attack time */
      for (i = 0; i < l->expectedChannels; ++i) {
        double v = n>=4 ? pow(10.0, atof(argv[3])/20) : 1.0;
        l->volume[i] = v;

        /* If there is a delay, store it. */
        if (n >= 5) l->delay = atof(argv[4]);
        else l->delay = 0.0;
      }
    return (AUDIO_EFFECT_SUCCESS);
}

static int parse_subarg(char *s, char **subargv, size_t *subargc) {
  char **ap;
  char *s_p;

  s_p = s;
  *subargc = 0;
  for (ap = subargv; (*ap = strtok(s_p, " \t")) != NULL;) {
    s_p = NULL;
    if (*subargc == 5) {
      ++*subargc;
      break;
    }
    if (**ap != '\0') {
      ++ap;
      ++*subargc;
    }
  }

  if (*subargc < 2 || *subargc > 5)
    {
      LogError("Wrong number of parameters for the compander effect within mcompand; usage:\n"
  "\tattack1,decay1{,attack2,decay2} [soft-knee-dB:]in-dB1[,out-dB1]{,in-dB2,out-dB2} [gain [initial-volume-dB [delay]]]\n"
  "\twhere {} means optional and repeatable and [] means optional.\n"
  "\tdB values are floating point or -inf'; times are in seconds.\n");
      return (AUDIO_EFFECT_EOF);
    } else
      return AUDIO_EFFECT_SUCCESS;
}

static int getopts(EffectContext * ctx, int argc, const char **argv)
{
  char *subargv[6], *cp;
  size_t subargc, i;

  priv_t * c = (priv_t *) ctx->priv;
  --argc, ++argv;

  c->band_buf1 = c->band_buf2 = c->band_buf3 = 0;
  c->band_buf_len = 0;

  /* how many bands? */
  if (! (argc&1)) {
    LogError("mcompand accepts only an odd number of arguments:\argc"
            "  mcompand quoted_compand_args [crossover_freq quoted_compand_args [...]].\n");
    return AUDIO_EFFECT_EOF;
  }
  c->nBands = (argc+1)>>1;

  c->bands = calloc(c->nBands, sizeof(comp_band_t));

  for (i=0;i<c->nBands;++i) {
    c->arg = av_strdup(argv[i<<1]);
    if (parse_subarg(c->arg,subargv,&subargc) != AUDIO_EFFECT_SUCCESS)
      return AUDIO_EFFECT_EOF;
    if (sox_mcompand_getopts_1(&c->bands[i], subargc, &subargv[0]) != AUDIO_EFFECT_SUCCESS)
      return AUDIO_EFFECT_EOF;
    free(c->arg);
    c->arg = NULL;
    if (i == (c->nBands-1))
      c->bands[i].topfreq = 0;
    else {
      c->bands[i].topfreq = lsx_parse_frequency(argv[(i<<1)+1],&cp);
      if (*cp) {
        LogError("bad frequency in args to mcompand.\n");
        return AUDIO_EFFECT_EOF;
      }
      if ((i>0) && (c->bands[i].topfreq < c->bands[i-1].topfreq)) {
        LogError("mcompand crossover frequencies must be in ascending order.\n");
        return AUDIO_EFFECT_EOF;
      }
    }
  }

  return AUDIO_EFFECT_SUCCESS;
}

/*
 * Prepare processing.
 * Do all initializations.
 */
static int start(EffectContext * ctx)
{
  priv_t * c = (priv_t *) ctx->priv;
  comp_band_t * l;
  size_t i;
  size_t band;

  for (band=0;band<c->nBands;++band) {
    l = &c->bands[band];
    l->delay_size = c->bands[band].delay * ctx->in_signal.sample_rate * ctx->in_signal.channels;
    if (l->delay_size > c->delay_buf_size)
      c->delay_buf_size = l->delay_size;
  }

  for (band=0;band<c->nBands;++band) {
    l = &c->bands[band];
    /* Convert attack and decay rates using number of samples */

    for (i = 0; i < l->expectedChannels; ++i) {
      if (l->attackRate[i] > 1.0/ctx->in_signal.sample_rate)
        l->attackRate[i] = 1.0 -
          exp(-1.0/(ctx->in_signal.sample_rate * l->attackRate[i]));
      else
        l->attackRate[i] = 1.0;
      if (l->decayRate[i] > 1.0/ctx->in_signal.sample_rate)
        l->decayRate[i] = 1.0 -
          exp(-1.0/(ctx->in_signal.sample_rate * l->decayRate[i]));
      else
        l->decayRate[i] = 1.0;
    }

    /* Allocate the delay buffer */
    if (c->delay_buf_size > 0)
      l->delay_buf = calloc(sizeof(long), c->delay_buf_size);
    l->delay_buf_ptr = 0;
    l->delay_buf_cnt = 0;

    if (l->topfreq != 0)
      crossover_setup(ctx, &l->filter, l->topfreq);
  }
  return (AUDIO_EFFECT_SUCCESS);
}

/*
 * Update a volume value using the given sample
 * value, the attack rate and decay rate
 */

static void doVolume(double *v, double samp, comp_band_t * l, size_t chan)
{
  double s = -samp / SOX_SAMPLE_MIN;
  double delta = s - *v;

  if (delta > 0.0) /* increase volume according to attack rate */
    *v += delta * l->attackRate[chan];
  else             /* reduce volume according to decay rate */
    *v += delta * l->decayRate[chan];
}

static int sox_mcompand_flow_1(EffectContext * ctx, priv_t * c, comp_band_t * l, const sample_type *ibuf, sample_type *obuf, size_t len, size_t filechans)
{
  size_t idone, odone;

  for (idone = 0, odone = 0; idone < len; ibuf += filechans) {
    size_t chan;

    /* Maintain the volume fields by simulating a leaky pump circuit */

    if (l->expectedChannels == 1 && filechans > 1) {
      /* User is expecting same compander for all channels */
      double maxsamp = 0.0;
      for (chan = 0; chan < filechans; ++chan) {
        double rect = fabs((double)ibuf[chan]);
        if (rect > maxsamp)
          maxsamp = rect;
      }
      doVolume(&l->volume[0], maxsamp, l, (size_t) 0);
    } else {
      for (chan = 0; chan < filechans; ++chan)
        doVolume(&l->volume[chan], fabs((double)ibuf[chan]), l, chan);
    }

    /* Volume memory is updated: perform compand */
    for (chan = 0; chan < filechans; ++chan) {
      int ch = l->expectedChannels > 1 ? chan : 0;
      double level_in_lin = l->volume[ch];
      double level_out_lin = lsx_compandt(&l->transfer_fn, level_in_lin);
      double checkbuf;

      if (c->delay_buf_size <= 0) {
        checkbuf = ibuf[chan] * level_out_lin;
        SOX_SAMPLE_CLIP(checkbuf);
        obuf[odone++] = checkbuf;
        idone++;
      } else {
        /* FIXME: note that this lookahead algorithm is really lame:
           the response to a peak is released before the peak
           arrives. */

        /* because volume application delays differ band to band, but
           total delay doesn't, the volume is applied in an iteration
           preceding that in which the sample goes to obuf, except in
           the band(s) with the longest vol app delay.

           the offset between delay_buf_ptr and the sample to apply
           vol to, is a constant equal to the difference between this
           band's delay and the longest delay of all the bands. */

        if (l->delay_buf_cnt >= l->delay_size) {
          checkbuf = l->delay_buf[(l->delay_buf_ptr + c->delay_buf_size - l->delay_size)%c->delay_buf_size] * level_out_lin;
          SOX_SAMPLE_CLIP(checkbuf);
          l->delay_buf[(l->delay_buf_ptr + c->delay_buf_size - l->delay_size)%c->delay_buf_size] = checkbuf;
        }
        if (l->delay_buf_cnt >= c->delay_buf_size) {
          obuf[odone] = l->delay_buf[l->delay_buf_ptr];
          odone++;
          idone++;
        } else {
          l->delay_buf_cnt++;
          idone++; /* no "odone++" because we did not fill obuf[...] */
        }
        l->delay_buf[l->delay_buf_ptr++] = ibuf[chan];
        l->delay_buf_ptr %= c->delay_buf_size;
      }
    }
  }

  if (idone != odone || idone != len) {
    /* Emergency brake - will lead to memory corruption otherwise since we
       cannot report back to flow() how many samples were consumed/emitted.
       Additionally, flow() doesn't know how to handle diverging
       sub-compander delays. */
    LogError("Using a compander delay within mcompand is currently not supported.\n");
    return (AUDIO_EFFECT_EOF);
    /* FIXME */
  }

  return (AUDIO_EFFECT_SUCCESS);
}

/*
 * Processed signed long samples from ibuf to obuf.
 * Return number of samples processed.
 */
static int flow(EffectContext * ctx, const sample_type *ibuf, sample_type *obuf,
                     size_t *isamp, size_t *osamp) {
  priv_t * c = (priv_t *) ctx->priv;
  comp_band_t * l;
  size_t len = min(*isamp, *osamp);
  size_t band, i;
  sample_type *abuf, *bbuf, *cbuf, *oldabuf, *ibuf_copy;
  double out;

  if (c->band_buf_len < len) {
    c->band_buf1 = realloc(c->band_buf1,len*sizeof(sample_type));
    c->band_buf2 = realloc(c->band_buf2,len*sizeof(sample_type));
    c->band_buf3 = realloc(c->band_buf3,len*sizeof(sample_type));
    c->band_buf_len = len;
  }

  len -= len % ctx->in_signal.channels;

  ibuf_copy = malloc(*isamp * sizeof(sample_type));
  memcpy(ibuf_copy, ibuf, *isamp * sizeof(sample_type));

  /* split ibuf into bands using filters, pipe each band through sox_mcompand_flow_1, then add back together and write to obuf */

  memset(obuf,0,len * sizeof *obuf);
  for (band=0,abuf=ibuf_copy,bbuf=c->band_buf2,cbuf=c->band_buf1;band<c->nBands;++band) {
    l = &c->bands[band];

    if (l->topfreq)
      crossover_flow(ctx, &l->filter, abuf, bbuf, cbuf, len);
    else {
      bbuf = abuf;
      abuf = cbuf;
    }
    if (abuf == ibuf_copy)
      abuf = c->band_buf3;
    (void)sox_mcompand_flow_1(ctx, c,l,bbuf,abuf,len, (size_t)ctx->in_signal.channels);
    for (i=0;i<len;++i)
    {
      out = (double)obuf[i] + (double)abuf[i];
      SOX_SAMPLE_CLIP(out);
      obuf[i] = out;
    }
    oldabuf = abuf;
    abuf = cbuf;
    cbuf = oldabuf;
  }

  *isamp = *osamp = len;

  free(ibuf_copy);

  return AUDIO_EFFECT_SUCCESS;
}

static int sox_mcompand_drain_1(EffectContext * effp, priv_t * c, comp_band_t * l, sample_type *obuf, size_t maxdrain)
{
  size_t done;
  double out;

  /*
   * Drain out delay samples.  Note that this loop does all channels.
   */
  for (done = 0;  done < maxdrain  &&  l->delay_buf_cnt > 0;  done++) {
    out = obuf[done] + l->delay_buf[l->delay_buf_ptr++];
    SOX_SAMPLE_CLIP(out);
    obuf[done] = out;
    l->delay_buf_ptr %= c->delay_buf_size;
    l->delay_buf_cnt--;
  }

  /* tell caller number of samples played */
  return done;

}

/*
 * Drain out compander delay lines.
 */
static int drain(EffectContext * ctx, sample_type *obuf, size_t *osamp)
{
  size_t band, drained, mostdrained = 0;
  priv_t * c = (priv_t *)ctx->priv;
  comp_band_t * l;

  *osamp -= *osamp % ctx->in_signal.channels;

  memset(obuf,0,*osamp * sizeof *obuf);
  for (band=0;band<c->nBands;++band) {
    l = &c->bands[band];
    drained = sox_mcompand_drain_1(ctx, c,l,obuf,*osamp);
    if (drained > mostdrained)
      mostdrained = drained;
  }

  *osamp = mostdrained;

  if (mostdrained)
      return AUDIO_EFFECT_SUCCESS;
  else
      return AUDIO_EFFECT_EOF;
}

/*
 * Clean up compander effect.
 */
static int stop(EffectContext * ctx)
{
  priv_t * c = (priv_t *) ctx->priv;
  comp_band_t * l;
  size_t band;

  free(c->band_buf1);
  c->band_buf1 = NULL;
  free(c->band_buf2);
  c->band_buf2 = NULL;
  free(c->band_buf3);
  c->band_buf3 = NULL;

  for (band = 0; band < c->nBands; band++) {
    l = &c->bands[band];
    free(l->delay_buf);
    if (l->topfreq != 0)
      free(l->filter.previous);
  }

  return AUDIO_EFFECT_SUCCESS;
}

static int lsx_kill(EffectContext * ctx)
{
  priv_t * c = (priv_t *) ctx->priv;
  comp_band_t * l;
  size_t band;

  for (band = 0; band < c->nBands; band++) {
    l = &c->bands[band];
    lsx_compandt_kill(&l->transfer_fn);
    free(l->decayRate);
    free(l->attackRate);
    free(l->volume);
  }
  free(c->arg);
  free(c->bands);
  c->bands = NULL;

  return AUDIO_EFFECT_SUCCESS;
}

static int mcompand_parseopts(EffectContext *ctx, const char *argv_s) {
#define MAX_ARGC 50
    const char *argv[MAX_ARGC];
    int argc = 0;
    argv[argc++] = ctx->handler.name;

    char *_argv_s = calloc(strlen(argv_s) + 1, sizeof(char));
    memcpy(_argv_s, argv_s, strlen(argv_s) + 1);

    char *token = strtok(_argv_s, ";");
    while (token != NULL) {
        argv[argc++] = token;
        token = strtok(NULL, ";");
    }

    int ret = getopts(ctx, argc, argv);
    if (ret < 0) goto end;
    ret = start(ctx);
    if (ret < 0) goto end;

end:
    free(_argv_s);
    return ret;
}

static int mcompand_set_mode(EffectContext *ctx, const char *mode) {
    LogInfo("%s mode = %s.\n", __func__, mode);
    priv_t *priv = (priv_t *)ctx->priv;
    if (0 == strcasecmp(mode, "None")) {
        return -1;
    } else {
        return mcompand_parseopts(ctx, MCOMPAND_PARAMS);
    }
}

static int mcompand_close(EffectContext *ctx) {
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
        stop(ctx);
        lsx_kill(ctx);
    }
    return 0;
}

static int mcompand_flush(EffectContext *ctx, void *samples,
                          const size_t max_nb_samples) {
    if(!ctx || !samples) return AEERROR_NULL_POINT;
    priv_t *priv = (priv_t *)ctx->priv;
    if (!priv || !priv->fifo_out || !priv->out_buf)
        return AEERROR_NULL_POINT;

    sdl_mutex_lock(priv->sdl_mutex);
    if (priv->effect_on) {
        size_t out_len = max_nb_samples;
        int completed = drain(ctx, priv->out_buf, &out_len);
        fifo_write(priv->fifo_out, priv->out_buf, out_len);
        while (completed == AUDIO_EFFECT_SUCCESS) {
            out_len = max_nb_samples;
            completed = drain(ctx, priv->out_buf, &out_len);
            fifo_write(priv->fifo_out, priv->out_buf, out_len);
        }
    } else {
        while (fifo_occupancy(priv->fifo_in) > 0) {
            size_t nb_samples =
                fifo_read(priv->fifo_in, priv->out_buf, max_nb_samples);
            fifo_write(priv->fifo_out, priv->out_buf, nb_samples);
        }
    }
    sdl_mutex_unlock(priv->sdl_mutex);

    return fifo_read(priv->fifo_out, samples, max_nb_samples);
}

static int mcompand_receive(EffectContext *ctx, void *samples,
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

static int mcompand_send(EffectContext *ctx, const void *samples,
                      const size_t nb_samples) {
    if(!ctx || !samples || nb_samples <= 0) return AEERROR_NULL_POINT;
    priv_t *priv = (priv_t *)ctx->priv;
    if (!priv || !priv->fifo_in) return AEERROR_NULL_POINT;

    return fifo_write(priv->fifo_in, samples, nb_samples);
}

static int mcompand_set(EffectContext *ctx, const char *key, int flags) {
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
            ret = mcompand_parseopts(ctx, entry->value);
            if (ret >= 0) priv->effect_on = true;
            else priv->effect_on = false;
        } else if (0 == strcasecmp(entry->key, "mode")) {
            ret = mcompand_set_mode(ctx, entry->value);
            if (ret >= 0) priv->effect_on = true;
            else priv->effect_on = false;
        }
        sdl_mutex_unlock(priv->sdl_mutex);
    }
    return ret;
}

static int mcompand_init(EffectContext *ctx, int argc, const char **argv) {
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
    if (ret < 0) mcompand_close(ctx);
    return ret;
}

const EffectHandler *effect_mcompand_fn(void) {
    static EffectHandler handler = {
        .name = "mcompand",
        .usage = mcompand_usage,
        .priv_size = sizeof(priv_t),
        .init = mcompand_init,
        .set = mcompand_set,
        .send = mcompand_send,
        .receive = mcompand_receive,
        .flush = mcompand_flush,
        .close = mcompand_close};
    return &handler;
}
