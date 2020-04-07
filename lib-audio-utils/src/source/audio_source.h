#ifndef _AUDIO_SOURCE_H_
#define _AUDIO_SOURCE_H_
#include "mixer/fade_in_out.h"
#include "codec/idecoder.h"
#include "codec/audio_decoder_factory.h"

typedef struct AudioSource {
    int crop_start_time_ms;
    int crop_end_time_ms;
    int start_time_ms;
    int end_time_ms;
    float volume;
    float left_factor;
    float right_factor;
    float yl_prev;
    float makeup_gain;
    bool side_chain_enable;
    char *file_path;
    IAudioDecoder *decoder;
    enum DecoderType decoder_type;
    FadeInOut fade_io;
} AudioSource;

void AudioSource_free(AudioSource *source);
void AudioSource_freep(AudioSource **source);
#endif
