#ifndef XM_AUDIO_EFFECTS_H_
#define XM_AUDIO_EFFECTS_H_

#include <stddef.h>

typedef struct XmEffectContext_T XmEffectContext;

#define AE_STATE_UNINIT  0
#define AE_STATE_INITIALIZED  1
#define AE_STATE_STARTED  2
#define AE_STATE_COMPLETED  3
#define AE_STATE_ERROR  4

/**
 * @brief free XmEffectContext
 *
 * @param ctx
 */
void xm_audio_effect_freep(XmEffectContext **ctx);

/**
 * @brief stop add audio effect
 *
 * @param ctx
 */
void xm_audio_effect_stop(XmEffectContext *ctx);

/**
 * @brief get progress
 *
 * @param ctx
 */
int xm_audio_effect_get_progress(XmEffectContext *ctx);

/**
 * @brief Add audio effects
 *
 * @param ctx XmEffectContext
 * @param in_pcm_path Input pcm file path
 * @param in_config_path Config file about audio effect parameter
 * @param out_pcm_path Output pcm file path
 * @return Less than 0 means failure
 */
int xm_audio_effect_add(XmEffectContext *ctx, const char *in_pcm_path, const char *in_config_path,
                    const char *out_pcm_path);

/**
 * @brief create XmEffectContext
 *
 * @param sample_rate The sample rate of input pcm data
 * @param channels The channels of input pcm data
 * @return XmEffectContext*
 */
XmEffectContext *xm_audio_effect_create(const int sample_rate, const int channels);

#endif  // XM_AUDIO_EFFECTS_H_
