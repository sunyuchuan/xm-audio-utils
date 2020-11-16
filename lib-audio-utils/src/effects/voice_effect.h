//
// Created by layne on 19-4-27.
//

#ifndef VOICE_EFFECT_H_
#define VOICE_EFFECT_H_

#include "effect_struct.h"

const EffectHandler *find_effect(char const *name);
EffectContext *create_effect(const EffectHandler *handler,
                             const int sample_rate, const int channels);
const char *show_usage(EffectContext *ctx);
int init_effect(EffectContext *ctx, int argc, const char **argv);
int set_effect(EffectContext *ctx, const char *key, const char *value,
               int flags);

/**
 * @brief send samples to effects
 *
 * @param ctx EffectContext
 * @param samples input samples buffer
 * @param nb_samples size of input buffer in short or byte
 * @return size of valid data sent.
        Less than 0 means failure
 */
int send_samples(EffectContext *ctx, const void *samples,
                 const size_t nb_samples);

/**
 * @brief obtain effects samples
 *
 * @param ctx EffectContext
 * @param samples output samples buffer
 * @param max_nb_samples size of expected data in short or byte
 * @return size of valid obtained.
        Less than 0 means failure
 */
int receive_samples(EffectContext *ctx, void *samples,
                    const size_t max_nb_samples);

int flush_effect(EffectContext *ctx, void *samples,
                 const size_t max_nb_samples);
void free_effect(EffectContext *ctx);

#endif  // AUDIO_EFFECTS_H_
