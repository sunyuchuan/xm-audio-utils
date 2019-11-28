#ifndef PCM_PARSER_H
#define PCM_PARSER_H
#include <stdio.h>
#include "tools/fifo.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct PcmParser {
    // seek parameters
    int seek_pos_ms;

    // Input parameters
    int src_sample_rate_in_Hz;
    int src_nb_channels;

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
} PcmParser;

void pcm_parser_free(PcmParser *parser);
void pcm_parser_freep(PcmParser **parser);
int pcm_parser_get_pcm_frame(PcmParser *parser,
    short *buffer, int buffer_size_in_short, bool loop);
int pcm_parser_seekTo(PcmParser *parser, int seek_pos_ms);
PcmParser *pcm_parser_create(const char *file_addr, int src_sample_rate,
    int src_nb_channels, int dst_sample_rate, int dst_nb_channels);

#endif // PCM_PARSER_H
