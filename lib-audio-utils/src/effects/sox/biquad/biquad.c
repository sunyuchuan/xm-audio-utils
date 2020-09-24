/* libSoX Biquad filter common functions   (c) 2006-7 robs@users.sourceforge.net
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

#include "biquad.h"
#include <string.h>

typedef biquad_t priv_t;

static char const * const width_str[] = {
  "band-width(Hz)",
  "band-width(kHz)",
  "band-width(Hz, no warp)", /* deprecated */
  "band-width(octaves)",
  "Q",
  "slope",
};
static char const all_width_types[] = "hkboqs";


int lsx_biquad_getopts(EffectContext * ctx, int argc, const char **argv,
    int min_args, int max_args, int fc_pos, int width_pos, int gain_pos,
    char const * allowed_width_types, filter_t filter_type)
{
  priv_t * p = (priv_t *)ctx->priv;
  char width_type = *allowed_width_types;
  char dummy, * dummy_p;     /* To check for extraneous chars. */
  --argc, ++argv;

  p->filter_type = filter_type;
  if (argc < min_args || argc > max_args ||
      (argc > fc_pos    && ((p->fc = lsx_parse_frequency(argv[fc_pos], &dummy_p)) <= 0 || *dummy_p)) ||
      (argc > width_pos && ((unsigned)(sscanf(argv[width_pos], "%lf%c %c", &p->width, &width_type, &dummy)-1) > 1 || p->width <= 0)) ||
      (argc > gain_pos  && sscanf(argv[gain_pos], "%lf %c", &p->gain, &dummy) != 1) ||
      !strchr(allowed_width_types, width_type) || (width_type == 's' && p->width > 1))
    return AUDIO_EFFECT_EOF;
  p->width_type = strchr(all_width_types, width_type) - all_width_types;
  if ((size_t)p->width_type >= strlen(all_width_types))
    p->width_type = 0;
  if (p->width_type == width_bw_kHz) {
    p->width *= 1000;
    p->width_type = width_bw_Hz;
  }
  return AUDIO_EFFECT_SUCCESS;
}


static int start(EffectContext * ctx)
{
  priv_t * p = (priv_t *)ctx->priv;
  /* Simplify: */
  p->b2 /= p->a0;
  p->b1 /= p->a0;
  p->b0 /= p->a0;
  p->a2 /= p->a0;
  p->a1 /= p->a0;

  p->o2 = p->o1 = p->i2 = p->i1 = 0;
  return AUDIO_EFFECT_SUCCESS;
}


int lsx_biquad_start(EffectContext * ctx)
{
  return start(ctx);
}


int lsx_biquad_flow(EffectContext * ctx, const sample_type *ibuf,
    sample_type *obuf, size_t *isamp, size_t *osamp)
{
  priv_t * p = (priv_t *)ctx->priv;
  size_t len = *isamp = *osamp = min(*isamp, *osamp);
  while (len--) {
    double o0 = *ibuf*p->b0 + p->i1*p->b1 + p->i2*p->b2 - p->o1*p->a1 - p->o2*p->a2;
    p->i2 = p->i1, p->i1 = *ibuf++;
    p->o2 = p->o1, p->o1 = o0;
    *obuf++ = SOX_ROUND_CLIP(o0);
  }
  return AUDIO_EFFECT_SUCCESS;
}

int create(EffectContext * ctx, int argc, const char * * argv)
{
  priv_t             * p = (priv_t *)ctx->priv;
  double             * d = &p->b0;
  char               c;

  --argc, ++argv;
  if (argc == 6)
    for (; argc && sscanf(*argv, "%lf%c", d, &c) == 1; --argc, ++argv, ++d);
  return argc? AUDIO_EFFECT_EOF : AUDIO_EFFECT_SUCCESS;
}
