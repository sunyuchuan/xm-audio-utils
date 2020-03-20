#ifndef PCM_PARSER_H
#define PCM_PARSER_H
#include <stdio.h>
#include "tools/fifo.h"
#include <stdbool.h>
#include <stdint.h>
#include "wav_dec.h"
#include<math.h>

#define BITS_PER_SAMPLE_16 16
#define BITS_PER_SAMPLE_8 8
#define PCM_FILE_EOF -1000
#define FLOAT_EPS 1e-6
#define DOUBLE_EPS 1e-15

static inline int calculation_duration_ms(int64_t size,
    float bytes_per_sample, int nb_channles, int sample_rate) {
    if (fabs(bytes_per_sample) <= FLOAT_EPS || nb_channles == 0
            || sample_rate == 0) {
        return 0;
    }
    return 1000 * (size / bytes_per_sample / nb_channles / sample_rate);
}

typedef struct PcmParser {
    // seek parameters
    int seek_pos_ms;
    // play-out volume.
    short volume_fix;
    float volume_flp;

    // Input parameters
    int src_sample_rate_in_Hz;
    int src_nb_channels;
    int bits_per_sample;
    int64_t pcm_start_pos;

    // Output parameters
    int dst_sample_rate_in_Hz;
    int dst_nb_channels;

    fifo *pcm_fifo;
    int max_src_buffer_size;
    short *src_buffer;
    int max_dst_buffer_size;
    short *dst_buffer;

    char* file_addr;
    int64_t file_size;
    FILE *reader;
    WavContext wav_ctx;
} PcmParser;

void pcm_parser_free(PcmParser *parser);
void pcm_parser_freep(PcmParser **parser);
int pcm_parser_get_pcm_frame(PcmParser *parser,
    short *buffer, int buffer_size_in_short, bool loop);
int pcm_parser_seekTo(PcmParser *parser, int seek_pos_ms);
PcmParser *pcm_parser_create(const char *file_addr,
    int src_sample_rate, int src_nb_channels, int dst_sample_rate,
    int dst_nb_channels, float volume_flp, WavContext *wav_ctx);

#endif // PCM_PARSER_H
