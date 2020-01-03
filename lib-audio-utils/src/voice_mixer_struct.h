#ifndef _VOICE_MIXER_STRUCT_
#define _VOICE_MIXER_STRUCT_
#include "effects/effect_struct.h"
#include "mixer_effects/audio_source_queue.h"

enum EffectType {
    NoiseSuppression = 0,
    Beautify,
    Reverb,
    VolumeLimiter,
    MAX_NB_EFFECTS
};

typedef struct VoiceEffcets {
    EffectContext *effects[MAX_NB_EFFECTS];
} VoiceEffcets;

typedef struct MixerEffcets {
    AudioSource *bgm;
    AudioSourceQueue *bgmQueue;
    AudioSource *music;
    AudioSourceQueue *musicQueue;
} MixerEffcets;

#endif
