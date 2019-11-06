#if defined(__ANDROID__) || defined (__linux__)

#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include "ffmpeg_utils.h"

typedef struct AudioDecoder {
    // seek parameters
    int seek_pos_ms;
    bool seek_req;

    // Output parameters
    int dst_sample_rate_in_Hz;
    int dst_nb_channels;

    AVAudioFifo* audio_fifo;
    uint8_t** copy_buffer;
    int max_nb_copy_samples;

    // Codec parameters
    AVFormatContext* fmt_ctx;
    AVCodecContext* dec_ctx;
    AVFrame* audio_frame;
    int audio_stream_index;

    // Resample parameters
    struct SwrContext* swr_ctx;
    int dst_nb_samples;
    int max_dst_nb_samples;
    uint8_t** dst_data;

    char* file_addr;
} AudioDecoder;

void xm_audio_decoder_free(AudioDecoder *decoder);
void xm_audio_decoder_freep(AudioDecoder **decoder);
int xm_audio_decoder_get_decoded_frame(AudioDecoder *decoder,
    short *buffer, int buffer_size_in_short, bool loop);
void xm_audio_decoder_seekTo(AudioDecoder *decoder, int seek_pos_ms);
AudioDecoder *xm_audio_decoder_create(const char *file_addr,
    int sample_rate, int channels);
#endif // AUDIO_DECODER_H
#endif // defined(__ANDROID__) || defined (__linux__)
