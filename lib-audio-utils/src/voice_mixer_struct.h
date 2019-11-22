#ifndef _VOICE_MIXER_STRUCT_
#define _VOICE_MIXER_STRUCT_
#include "effects/effect_struct.h"
#include "mixer_effects/fade_in_out.h"
#include "codec/audio_decoder.h"

enum EffectType {
    NoiseSuppression = 0,
    Beautify,
    VolumeLimiter,
    MAX_NB_EFFECTS
};

typedef struct VoiceEffcets {
    int nb_effects;
    EffectContext *effects[MAX_NB_EFFECTS];
} VoiceEffcets;

typedef struct BgmMusic {
    FadeInOut fade_io;
    char *url;
    float volume;
    int start_time_ms;
    int end_time_ms;
    float left_factor;
    float right_factor;
    float yl_prev;
    bool side_chain_enable;
    float makeup_gain;
    AudioDecoder *decoder;
} BgmMusic;

typedef struct MixerEffcets {
    int nb_bgms;
    int bgms_index;
    BgmMusic **bgms;
    int nb_musics;
    int musics_index;
    BgmMusic **musics;
} MixerEffcets;

#endif
