#ifndef XM_AUDIO_EFFECTS_H_
#define XM_AUDIO_EFFECTS_H_

#include <stddef.h>
#include "idecoder.h"

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
 * @brief get AudioDecoder
 *
 * @param ctx XmEffectContext
 * @return IAudioDecoder*
 */
IAudioDecoder *xm_audio_effect_get_decoder(XmEffectContext *ctx);

/**
 * @brief Get frame data with voice effects
 *
 * @param ctx XmEffectContext
 * @param buffer buffer for storing data
 * @param buffer_size_in_short buffer size
 * @return size of valid buffer obtained.
                  Less than or equal to 0 means failure or end
 */
int xm_audio_effect_get_frame(XmEffectContext *ctx,
    short *buffer, int buffer_size_in_short);

/**
 * @brief file seekTo
 *
 * @param ctx XmEffectContext
 * @param seek_time_ms seek target time in ms
 * @return Less than 0 means failure
 */
int xm_audio_effect_seekTo(XmEffectContext *ctx,
    int seek_time_ms);

/**
 * @brief Add audio effects
 *
 * @param ctx XmEffectContext
 * @param out_pcm_path Output pcm file path
 * @return Less than 0 means failure
 */
int xm_audio_effect_add_effects(XmEffectContext *ctx,
    const char *out_pcm_path);

/**
 * @brief effect init
 *
 * @param ctx XmEffectContext
 * @param in_config_path Config file about effects parameter
 * @return Less than 0 means failure
 */
int xm_audio_effect_init(XmEffectContext *ctx,
    const char *in_config_path);

/**
 * @brief create XmEffectContext
 *
 * @return XmEffectContext*
 */
XmEffectContext *xm_audio_effect_create();

#endif  // XM_AUDIO_EFFECTS_H_
