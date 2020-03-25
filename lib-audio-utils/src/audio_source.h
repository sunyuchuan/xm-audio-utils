#ifndef _AUDIO_SOURCE_H_
#define _AUDIO_SOURCE_H_
#include "mixer_effects/fade_in_out.h"
#include "idecoder.h"
#include "audio_decoder_factory.h"

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
    IAudioDecoder *decoder;
    enum DecoderType decoder_type;
    FadeInOut fade_io;
} AudioSource;

typedef struct AudioRecordSource {
    int start_time_ms;
    int end_time_ms;
    int sample_rate;
    int nb_channels;
    float volume;
    char *file_path;
    IAudioDecoder *decoder;
    enum DecoderType decoder_type;
} AudioRecordSource;

void audio_source_free(AudioSource *source);
void audio_source_freep(AudioSource **source);
void audio_record_source_free(AudioRecordSource *record);
void audio_record_source_freep(AudioRecordSource **record);
#endif
