#ifndef _AUDIO_RECORD_SOURCE_H_
#define _AUDIO_RECORD_SOURCE_H_
#include "codec/idecoder.h"
#include "codec/audio_decoder_factory.h"

typedef struct AudioRecordSource {
    int crop_start_time_ms;
    int crop_end_time_ms;
    int start_time_ms;
    int end_time_ms;
    int sample_rate;
    int nb_channels;
    float volume;
    char *file_path;
    IAudioDecoder *decoder;
    enum DecoderType decoder_type;
} AudioRecordSource;

void AudioRecordSource_free(AudioRecordSource *record);
void AudioRecordSource_freep(AudioRecordSource **record);
#endif //_AUDIO_RECORD_SOURCE_H_