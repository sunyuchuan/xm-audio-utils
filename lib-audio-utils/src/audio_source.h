#ifndef _AUDIO_SOURCE_H_
#define _AUDIO_SOURCE_H_
#include "mixer_effects/fade_in_out.h"
#include "pcm_parser.h"
#include "wav_dec.h"

typedef struct AudioSource {
    int sample_rate;
    int nb_channels;
    int start_time_ms;
    int end_time_ms;
    float volume;
    float left_factor;
    float right_factor;
    float yl_prev;
    float makeup_gain;
    bool side_chain_enable;
    char *file_path;
    PcmParser *parser;
    FadeInOut fade_io;
    WavHeader wav_header;
} AudioSource;

typedef struct AudioRecordSource {
    int start_time_ms;
    int end_time_ms;
    int sample_rate;
    int nb_channels;
    char *file_path;
    WavHeader wav_header;
} AudioRecordSource;

void audio_source_free(AudioSource *source);
void audio_source_freep(AudioSource **source);
void audio_record_source_free(AudioRecordSource *record);
void audio_record_source_freep(AudioRecordSource **record);
#endif
