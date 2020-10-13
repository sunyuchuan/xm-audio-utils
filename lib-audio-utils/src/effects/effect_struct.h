#ifndef _EFFECT_STRUCT_H_
#define _EFFECT_STRUCT_H_
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include "tools/dict.h"
#include "error_def.h"
#include "log.h"

#ifndef MAX_NB_SAMPLES
#define MAX_NB_SAMPLES (1024 << 3)
#endif

#define REVERB_PARAMS "1.5 50.0 100.0 0.0 -6.0"
#define REVERB_PARAMS_SOX "1.5 50.0 100.0 50.0 0.0 -6.0"
#define COMPAND_PARAMS "0.3,1 6:-70,-60,-20 -5 -90 0.2"
#define MCOMPAND_PARAMS \
    "0.005,0.1 -47,-40,-34,-34,-17,-33;100; \
    0.003,0.05 -47,-40,-34,-34,-17,-33;400; \
    0.000625,0.0125 -47,-40,-34,-34,-15,-33;1600; \
    0.0001,0.025 -47,-40,-34,-34,-31,-31,-0,-30;6400; \
    0,0.025 -38,-31,-28,-28,-0,-25;"
#define EQUALIZER_PARAMS "89.5 1.5q 5.8"
#define BIQUADS_PARAMS "89.5 1.5q 5.8"

typedef struct EffectContext_T EffectContext;
typedef struct EffectHandler_T EffectHandler;
typedef const EffectHandler *(*effect_fn)(void);

struct EffectHandler_T {
    const char *name;
    const char *usage;
    size_t priv_size;

    int (*init)(EffectContext *ctx, int argc, const char **argv);
    int (*set)(EffectContext *ctx, const char *key, int flags);
    int (*send)(EffectContext *ctx, const void *samples,
                const size_t nb_samples);
    int (*receive)(EffectContext *ctx, void *samples, const size_t nb_samples);
    int (*flush)(EffectContext *ctx, void *samples, const size_t nb_samples);
    int (*close)(EffectContext *ctx);
};

typedef struct SignalInfoT {
    int sample_rate; /**< samples per second, 0 if unknown */
    int channels;    /**< number of sound channels, 0 if unknown */
    double * mult;   /**< Effects headroom multiplier; may be null */
} SignalInfo;

struct EffectContext_T {
    EffectHandler handler;
    AEDictionary *options;
    atomic_bool return_max_nb_samples;
    SignalInfo in_signal;
    void *priv;
};

#define NUMERIC_PARAMETER(name, min, max)                                   \
    {                                                                       \
        char *end_ptr;                                                      \
        float d;                                                            \
        if (argc == 0) break;                                               \
        d = strtod(*argv, &end_ptr);                                        \
        if (end_ptr != *argv) {                                             \
            if (d < min || d > max || *end_ptr != '\0') {                   \
                LogError("parameter `%s' must be between %g and %g", #name, \
                         (float)min, (float)max);                           \
                return AUDIO_EFFECT_EOF;                                    \
            }                                                               \
            priv->name = d;                                                 \
            --argc, ++argv;                                                 \
        }                                                                   \
    }

extern const char *show_usage(EffectContext *ctx);

#endif  // AUDIO_EFFECT_EFFECT_STRUCT_H_
