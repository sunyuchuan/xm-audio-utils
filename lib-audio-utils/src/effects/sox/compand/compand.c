/* libSoX compander effect
 *
 * Written by Nick Bailey (nick@bailey-family.org.uk or
 *                         n.bailey@elec.gla.ac.uk)
 *
 * Copyright 1999 Chris Bagwell And Nick Bailey
 * This source code is freely redistributable and may be used for
 * any purpose.  This copyright notice must be maintained.
 * Chris Bagwell And Nick Bailey are not responsible for
 * the consequences of using this software.
 */

#include "libavutil/mem.h"
#include "compandt.h"
#include "error_def.h"
#include "tools/fifo.h"
#include "tools/sdl_mutex.h"

/*
 * Compressor/expander effect for libSoX.
 *
 * Flow diagram for one channel:
 *
 *               ------------      ---------------
 *              |            |    |               |     ---
 * ibuff ---+---| integrator |--->| transfer func |--->|   |
 *          |   |            |    |               |    |   |
 *          |    ------------      ---------------     |   |  * gain
 *          |                                          | * |----------->obuff
 *          |       -------                            |   |
 *          |      |       |                           |   |
 *          +----->| delay |-------------------------->|   |
 *                 |       |                            ---
 *                  -------
 */
#define compand_usage \
  "attack1,decay1{,attack2,decay2} [soft-knee-dB:]in-dB1[,out-dB1]{,in-dB2,out-dB2} [gain [initial-volume-dB [delay]]]\n" \
  "\twhere {} means optional and repeatable and [] means optional.\n" \
  "\tdB values are floating point or -inf'; times are in seconds."
/*
 * Note: clipping can occur if the transfer function pushes things too
 * close to 0 dB.  In that case, use a negative gain, or reduce the
 * output level of the transfer function.
 */

typedef struct {
  fifo *fifo_in;
  fifo *fifo_out;
  SdlMutex *sdl_mutex;
  bool effect_on;
  sample_type *in_buf;
  sample_type *out_buf;

  sox_compandt_t transfer_fn;

  struct {
    double attack_times[2]; /* 0:attack_time, 1:decay_time */
    double volume;          /* Current "volume" of each channel */
  } * channels;
  unsigned expectedChannels;/* Also flags that channels aren't to be treated
                               individually when = 1 and input not mono */
  double delay;             /* Delay to apply before companding */
  sample_type *delay_buf;   /* Old samples, used for delay processing */
  ptrdiff_t delay_buf_size;/* Size of delay_buf in samples */
  ptrdiff_t delay_buf_index; /* Index into delay_buf */
  ptrdiff_t delay_buf_cnt; /* No. of active entries in delay_buf */
  int delay_buf_full;       /* Shows buffer situation (important for drain) */

  char *arg0;  /* copies of arguments, so that they may be modified */
  char *arg1;
  char *arg2;
} priv_t;

static int getopts(EffectContext *ctx, int argc, const char **argv)
{
  priv_t * l = (priv_t *) ctx->priv;
  char * s;
  char dummy;     /* To check for extraneous chars. */
  unsigned pairs, i, j, commas;

  --argc, ++argv;
  if (argc < 2 || argc > 5) {
    if (ctx->handler.usage)
      LogError("usage: %s.\n", ctx->handler.usage);
    else
      LogError("this effect takes no parameters.\n");
    return AUDIO_EFFECT_EOF;
  }

  l->arg0 = av_strdup(argv[0]);
  l->arg1 = av_strdup(argv[1]);
  l->arg2 = argc > 2 ? av_strdup(argv[2]) : NULL;

  /* Start by checking the attack and decay rates */
  for (s = l->arg0, commas = 0; *s; ++s) if (*s == ',') ++commas;
  if ((commas % 2) == 0) {
    LogError("there must be an even number of attack/decay parameters.\n");
    return AUDIO_EFFECT_EOF;
  }
  pairs = 1 + commas/2;
  l->channels = calloc(pairs, sizeof(*l->channels));
  l->expectedChannels = pairs;

  /* Now tokenise the rates string and set up these arrays.  Keep
     them in seconds at the moment: we don't know the sample rate yet. */
  for (i = 0, s = strtok(l->arg0, ","); s != NULL; ++i) {
    for (j = 0; j < 2; ++j) {
      if (sscanf(s, "%lf %c", &l->channels[i].attack_times[j], &dummy) != 1) {
        LogError("syntax error trying to read attack/decay time.\n");
        return AUDIO_EFFECT_EOF;
      } else if (l->channels[i].attack_times[j] < 0) {
        LogError("attack & decay times can't be less than 0 seconds.\n");
        return AUDIO_EFFECT_EOF;
      }
      s = strtok(NULL, ",");
    }
  }

  if (!lsx_compandt_parse(&l->transfer_fn, l->arg1, l->arg2))
    return AUDIO_EFFECT_EOF;

  /* Set the initial "volume" to be attibuted to the input channels.
     Unless specified, choose 0dB otherwise clipping will
     result if the user has seleced a long attack time */
  for (i = 0; i < l->expectedChannels; ++i) {
    double init_vol_dB = 0;
    if (argc > 3 && sscanf(argv[3], "%lf %c", &init_vol_dB, &dummy) != 1) {
      LogError("syntax error trying to read initial volume.\n");
      return AUDIO_EFFECT_EOF;
    } else if (init_vol_dB > 0) {
      LogError("initial volume is relative to maximum volume so can't exceed 0dB.\n");
      return AUDIO_EFFECT_EOF;
    }
    l->channels[i].volume = pow(10., init_vol_dB / 20);
  }

  /* If there is a delay, store it. */
  if (argc > 4 && sscanf(argv[4], "%lf %c", &l->delay, &dummy) != 1) {
    LogError("syntax error trying to read delay value.\n");
    return AUDIO_EFFECT_EOF;
  } else if (l->delay < 0) {
    LogError("delay can't be less than 0 seconds.\n");
    return AUDIO_EFFECT_EOF;
  }

  return AUDIO_EFFECT_SUCCESS;
}

static int start(EffectContext *ctx)
{
  priv_t * l = (priv_t *) ctx->priv;
  unsigned i, j;

  LogDebug("%i input channel(s) expected: actually %i.\n",
      l->expectedChannels, ctx->in_signal.channels);
  for (i = 0; i < l->expectedChannels; ++i)
    LogDebug("Channel %i: attack = %g decay = %g.\n", i,
        l->channels[i].attack_times[0], l->channels[i].attack_times[1]);

  /* Convert attack and decay rates using number of samples */
  for (i = 0; i < l->expectedChannels; ++i)
    for (j = 0; j < 2; ++j)
      if (l->channels[i].attack_times[j] > 1.0/ctx->in_signal.sample_rate)
        l->channels[i].attack_times[j] = 1.0 -
          exp(-1.0/(ctx->in_signal.sample_rate * l->channels[i].attack_times[j]));
      else
        l->channels[i].attack_times[j] = 1.0;

  /* Allocate the delay buffer */
  l->delay_buf_size = l->delay * ctx->in_signal.sample_rate * ctx->in_signal.channels;
  if (l->delay_buf_size > 0)
    l->delay_buf = calloc((size_t)l->delay_buf_size, sizeof(*l->delay_buf));
  l->delay_buf_index = 0;
  l->delay_buf_cnt = 0;
  l->delay_buf_full= 0;

  return AUDIO_EFFECT_SUCCESS;
}

/*
 * Update a volume value using the given sample
 * value, the attack rate and decay rate
 */
static void doVolume(double *v, double samp, priv_t * l, int chan)
{
  double s = -samp / SOX_SAMPLE_MIN;
  double delta = s - *v;

  if (delta > 0.0) /* increase volume according to attack rate */
    *v += delta * l->channels[chan].attack_times[0];
  else             /* reduce volume according to decay rate */
    *v += delta * l->channels[chan].attack_times[1];
}

static int flow(EffectContext * ctx, const sample_type *ibuf, sample_type *obuf,
                    size_t *isamp, size_t *osamp)
{
  priv_t * l = (priv_t *) ctx->priv;
  int len =  (*isamp > *osamp) ? *osamp : *isamp;
  int filechans = ctx->in_signal.channels;
  int idone,odone;

  for (idone = 0,odone = 0; idone < len; ibuf += filechans) {
    int chan;

    /* Maintain the volume fields by simulating a leaky pump circuit */
    for (chan = 0; chan < filechans; ++chan) {
      if (l->expectedChannels == 1 && filechans > 1) {
        /* User is expecting same compander for all channels */
        int i;
        double maxsamp = 0.0;
        for (i = 0; i < filechans; ++i) {
          double rect = fabs((double)ibuf[i]);
          if (rect > maxsamp) maxsamp = rect;
        }
        doVolume(&l->channels[0].volume, maxsamp, l, 0);
        break;
      } else
        doVolume(&l->channels[chan].volume, fabs((double)ibuf[chan]), l, chan);
    }

    /* Volume memory is updated: perform compand */
    for (chan = 0; chan < filechans; ++chan) {
      int ch = l->expectedChannels > 1 ? chan : 0;
      double level_in_lin = l->channels[ch].volume;
      double level_out_lin = lsx_compandt(&l->transfer_fn, level_in_lin);
      double checkbuf;

      if (l->delay_buf_size <= 0) {
        checkbuf = ibuf[chan] * level_out_lin;
        SOX_SAMPLE_CLIP(checkbuf);
        obuf[odone++] = checkbuf;
        idone++;
      } else {
        if (l->delay_buf_cnt >= l->delay_buf_size) {
          l->delay_buf_full=1; /* delay buffer is now definitely full */
          checkbuf = l->delay_buf[l->delay_buf_index] * level_out_lin;
          SOX_SAMPLE_CLIP(checkbuf);
          obuf[odone] = checkbuf;
          odone++;
          idone++;
        } else {
          l->delay_buf_cnt++;
          idone++; /* no "odone++" because we did not fill obuf[...] */
        }
        l->delay_buf[l->delay_buf_index++] = ibuf[chan];
        l->delay_buf_index %= l->delay_buf_size;
      }
    }
  }

  *isamp = idone; *osamp = odone;
  return (AUDIO_EFFECT_SUCCESS);
}

static int drain(EffectContext * ctx, sample_type *obuf, size_t *osamp)
{
  priv_t * l = (priv_t *) ctx->priv;
  size_t chan, done = 0;
  double checkbuf;

  if (l->delay_buf_full == 0)
    l->delay_buf_index = 0;
  while (done+ctx->in_signal.channels <= *osamp && l->delay_buf_cnt > 0)
    for (chan = 0; chan < (size_t)ctx->in_signal.channels; ++chan) {
      int c = l->expectedChannels > 1 ? chan : 0;
      double level_in_lin = l->channels[c].volume;
      double level_out_lin = lsx_compandt(&l->transfer_fn, level_in_lin);
      checkbuf = l->delay_buf[l->delay_buf_index++] * level_out_lin;
      SOX_SAMPLE_CLIP(checkbuf);
      obuf[done++] = checkbuf;
      l->delay_buf_index %= l->delay_buf_size;
      l->delay_buf_cnt--;
    }
  *osamp = done;
  return l->delay_buf_cnt > 0 ? AUDIO_EFFECT_SUCCESS : AUDIO_EFFECT_EOF;
}

static int stop(EffectContext * ctx)
{
  priv_t * l = (priv_t *) ctx->priv;

  free(l->delay_buf);
  return AUDIO_EFFECT_SUCCESS;
}

static int lsx_kill(EffectContext * ctx)
{
  priv_t * l = (priv_t *) ctx->priv;

  lsx_compandt_kill(&l->transfer_fn);
  free(l->channels);
  free(l->arg0);
  free(l->arg1);
  free(l->arg2);
  return AUDIO_EFFECT_SUCCESS;
}

static int compand_parseopts(EffectContext *ctx, const char *argv_s) {
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

static int compand_set_mode(EffectContext *ctx, const char *mode) {
    LogInfo("%s mode = %s.\n", __func__, mode);
    priv_t *priv = (priv_t *)ctx->priv;
    if (0 == strcasecmp(mode, "None")) {
        return -1;
    } else {
        return compand_parseopts(ctx, COMPAND_PARAMS);
    }
}

static int compand_close(EffectContext *ctx) {
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

static int compand_flush(EffectContext *ctx, void *samples,
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

static int compand_receive(EffectContext *ctx, void *samples,
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

static int compand_send(EffectContext *ctx, const void *samples,
                      const size_t nb_samples) {
    if(!ctx || !samples || nb_samples <= 0) return AEERROR_NULL_POINT;
    priv_t *priv = (priv_t *)ctx->priv;
    if (!priv || !priv->fifo_in) return AEERROR_NULL_POINT;

    return fifo_write(priv->fifo_in, samples, nb_samples);
}

static int compand_set(EffectContext *ctx, const char *key, int flags) {
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
            ret = compand_parseopts(ctx, entry->value);
            if (ret >= 0) priv->effect_on = true;
            else priv->effect_on = false;
        } else if (0 == strcasecmp(entry->key, "mode")) {
            ret = compand_set_mode(ctx, entry->value);
            if (ret >= 0) priv->effect_on = true;
            else priv->effect_on = false;
        }
        sdl_mutex_unlock(priv->sdl_mutex);
    }
    return ret;
}

static int compand_init(EffectContext *ctx, int argc, const char **argv) {
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
    if (ret < 0) compand_close(ctx);
    return ret;
}

const EffectHandler *effect_compand_fn(void) {
    static EffectHandler handler = {
        .name = "compand",
        .usage = compand_usage,
        .priv_size = sizeof(priv_t),
        .init = compand_init,
        .set = compand_set,
        .send = compand_send,
        .receive = compand_receive,
        .flush = compand_flush,
        .close = compand_close};
    return &handler;
}
