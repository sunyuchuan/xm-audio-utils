#ifndef XM_TOTAL_EFFECTS_H_
#define XM_TOTAL_EFFECTS_H_

#include <stddef.h>
#include <stdlib.h>
#include "tools/fifo.h"
#include "effects/effect_struct.h"
#include "tools/sdl_mutex.h"

typedef struct EffectsInfo {
    char *name;
    char *info;
} EffectsInfo;

#define MAX_NB_TOTAL_EFFECTS 20
#define MAX_NB_CHANNELS 2
typedef struct TotalEffectContext {
    int sample_rate;
    int channels;
    int bits_per_sample;
    fifo *fifo_in;
    fifo *fifo_out;
    short *buffer;
    short *buffer_L;
    short *buffer_R;
    SdlMutex *sdl_mutex;
    EffectContext *effects[MAX_NB_TOTAL_EFFECTS][MAX_NB_CHANNELS];
} TotalEffectContext;

/**
 * @brief free TotalEffectContext
 *
 * @param ctx
 */
void total_effect_freep(TotalEffectContext **ctx);

/**
 * @brief flush total effecter
 *
 * @param ctx TotalEffectContext
 * @param buffer output buffer
 * @param buffer_size_in_short size of expected data in short
 * @return size of valid obtained.
        Less than 0 means failure
 */
int total_effect_flush(TotalEffectContext *ctx,
                       short *buffer, int buffer_size_in_short);

/**
 * @brief obtain total effects samples
 *
 * @param ctx TotalEffectContext
 * @param buffer output buffer
 * @param buffer_size_in_short size of expected data in short
 * @return size of valid obtained.
        Less than 0 means failure
 */
int total_effect_receive(TotalEffectContext *ctx,
                         short *buffer, int buffer_size_in_short);

/**
 * @brief send samples to total effects
 *
 * @param ctx TotalEffectContext
 * @param buffer input samples buffer
 * @param buffer_size_in_short size of input buffer in short
 * @return size of valid data sent.
        Less than 0 means failure
 */
int total_effect_send(TotalEffectContext *ctx,
                      short *buffer, int buffer_size_in_short);

/**
 * @brief init TotalEffectContext
 *
 * @param ctx TotalEffectContext
 * @param effects_info total effects info
 * @param sample_rate sample rate of pcm data
 * @param channels number of channels
 * @param bits_per_sample Sampling depth
 * @return Less than 0 means failure
 */
int total_effect_init(TotalEffectContext *ctx,
                      EffectsInfo *effects_info, int sample_rate,
                      int channels, int bits_per_sample);

/**
 * @brief create TotalEffectContext
 *
 * @return TotalEffectContext*
 */
TotalEffectContext *total_effect_create();

#endif  // XM_TOTAL_EFFECTS_H_
