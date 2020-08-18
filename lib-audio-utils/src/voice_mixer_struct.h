#ifndef _VOICE_MIXER_STRUCT_
#define _VOICE_MIXER_STRUCT_
#include "effects/effect_struct.h"
#include "source/audio_source_queue.h"
#include "source/audio_record_source_queue.h"

// Limit the maximum duration of a mix
//#define MAX_DURATION_MIX_IN_MS (50*60*1000)

enum EffectType {
    NoiseSuppression = 0,
    Beautify,
    Reverb,
    Minions,
    VoiceMorph,
    VolumeLimiter,
    MAX_NB_EFFECTS
};

typedef struct VoiceEffects {
    int duration_ms;
    AudioRecordSource *record;
    AudioRecordSourceQueue *recordQueue;
    EffectContext *effects[MAX_NB_EFFECTS];
    char *effects_info[MAX_NB_EFFECTS];
} VoiceEffects;

typedef struct MixerEffects {
    int duration_ms;
    AudioSource *bgm;
    AudioSourceQueue *bgmQueue;
    AudioSource *music;
    AudioSourceQueue *musicQueue;
} MixerEffects;

#endif
