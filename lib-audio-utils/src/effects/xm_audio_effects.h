#ifndef XM_AUDIO_EFFECTS_H_
#define XM_AUDIO_EFFECTS_H_

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include "tools/fifo.h"
#include "voice_mixer_struct.h"
#include "codec/idecoder.h"
#include "codec/ffmpeg_utils.h"

typedef struct XmEffectContext {
    volatile bool abort;
    volatile bool flush;
    volatile bool is_zero;
    int ae_status;
    int progress;
    // output pcm sample rate and number channels
    int dst_sample_rate;
    int dst_channels;
    int dst_bits_per_sample;
    // input record audio file seek position
    int seek_time_ms;
    // input record audio file read location
    int64_t cur_size;
    int duration_ms;
    short buffer[MAX_NB_SAMPLES];
    char *in_config_path;
    fifo *audio_fifo;
    pthread_mutex_t mutex;
    VoiceEffects voice_effects;
} XmEffectContext;

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
