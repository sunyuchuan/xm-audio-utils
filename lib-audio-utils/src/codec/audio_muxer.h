#ifndef _AUDIO_MUXER_H_
#define _AUDIO_MUXER_H_
#include "muxer_config.h"
#include <pthread.h>
#include "ffmpeg_utils.h"
#include "audio_encoder.h"

typedef struct AudioMuxer {
    volatile bool abort;
    // Used to copy buffer
    uint8_t **copy_buffer;
    int max_nb_copy_samples;

    // Resample parameters
    uint8_t **dst_data;
    int max_dst_nb_samples;
    struct SwrContext *swr_ctx;

    // Codec parameters
    AVFormatContext *ofmt_ctx;
    AVCodecContext *enc_ctx;
    AVAudioFifo *encode_fifo;
    AVFrame *audio_frame;
    int audio_stream_index;
    int64_t audio_encode_pts;

    // audio encoder
    Encoder *audio_encoder;
    int frame_size;

    pthread_mutex_t mutex;
    MuxerConfig config;
} AudioMuxer;

void muxer_free(AudioMuxer *am);
void muxer_freep(AudioMuxer **am);
int muxer_stop(AudioMuxer *am);
int muxer_write_audio_frame(AudioMuxer *am, const short *buffer,
                            int buffer_size_in_short);
AudioMuxer *muxer_create(MuxerConfig *config);

#endif // _AUDIO_MUXER_H_
