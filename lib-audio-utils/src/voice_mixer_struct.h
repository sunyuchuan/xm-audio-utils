#ifndef _VOICE_MIXER_STRUCT_
#define _VOICE_MIXER_STRUCT_
#include "effects/effect_struct.h"
#include "audio_source_queue.h"

// Limit the maximum duration of a mix
#define MAX_DURATION_MIX_IN_MS (25*60*1000)

enum EffectType {
    NoiseSuppression = 0,
    Beautify,
    Reverb,
    VolumeLimiter,
    MAX_NB_EFFECTS
};

typedef struct VoiceEffcets {
    AudioRecordSource *record;
    EffectContext *effects[MAX_NB_EFFECTS];
    int dst_sample_rate;
    int dst_channels;
} VoiceEffcets;

typedef struct MixerEffcets {
    AudioRecordSource *record;
    AudioSource *bgm;
    AudioSourceQueue *bgmQueue;
    AudioSource *music;
    AudioSourceQueue *musicQueue;
} MixerEffcets;

#endif
