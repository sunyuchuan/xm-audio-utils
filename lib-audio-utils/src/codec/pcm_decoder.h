#ifndef PCM_DECODER_H
#define PCM_DECODER_H
#include "idecoder.h"

IAudioDecoder *PcmDecoder_create(const char *file_addr,
    int src_sample_rate, int src_nb_channels, int dst_sample_rate,
    int dst_nb_channels, float volume_flp);

int get_pcm_file_duration_ms(const char *file_addr,
    int bits_per_sample, int src_sample_rate_in_Hz, int src_nb_channels);

#endif // PCM_DECODER_H