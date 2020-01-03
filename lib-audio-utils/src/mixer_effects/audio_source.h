#ifndef _AUDIO_SOURCE_H_
#define _AUDIO_SOURCE_H_
#include "fade_in_out.h"
#include "../codec/audio_decoder.h"

typedef struct AudioSource {
    int start_time_ms;
    int end_time_ms;
    float volume;
    float left_factor;
    float right_factor;
    float yl_prev;
    float makeup_gain;
    bool side_chain_enable;
    char *file_path;
    AudioDecoder *decoder;
    FadeInOut fade_io;
} AudioSource;

#endif
