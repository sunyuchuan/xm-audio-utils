#include "pcm_parser.h"
#include "log.h"
#include "error_def.h"
#include "tools/util.h"
#include "effects/effect_struct.h"
#include "tools/mem.h"
#include <stdlib.h>
#include <inttypes.h>

#define PCM_FILE_EOF -1000

inline static int64_t align(int64_t x, int align) {
    return ((( x ) + (align) - 1) / (align) * (align));
}

static int write_fifo(PcmParser *parser) {
    int ret = -1;
    if (!parser || !parser->reader)
        return ret;

    if (feof(parser->reader) || ferror(parser->reader)) {
        ret = PCM_FILE_EOF;
        goto end;
    }

    int read_len = fread(parser->src_buffer, 2, parser->max_src_buffer_size,
        parser->reader);
    if (read_len <= 0) {
        ret = PCM_FILE_EOF;
        goto end;
    }

    if (parser->src_nb_channels != parser->dst_nb_channels) {
        int write_size = 0;
        if (parser->src_nb_channels == 1) {
            MonoToStereoS16(parser->dst_buffer, parser->src_buffer, read_len);
            write_size = read_len << 1;
        } else if (parser->src_nb_channels == 2) {
            StereoToMonoS16(parser->dst_buffer, parser->src_buffer, read_len);
            write_size = read_len >> 1;
        }
        ret = fifo_write(parser->pcm_fifo, parser->dst_buffer, write_size);
        if (ret < 0) goto end;
        ret = write_size;
    } else {
        ret = fifo_write(parser->pcm_fifo, parser->src_buffer, read_len);
        if (ret < 0) goto end;
        ret = read_len;
    }

end:
    return ret;
}

static int init_parser(PcmParser *parser, const char *file_addr,
        int src_sample_rate, int src_nb_channels,
        int dst_sample_rate, int dst_nb_channels) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if (!parser || !file_addr)
        return ret;

    char *tmp_file_addr = NULL;
    if ((ret = CopyString(file_addr, &tmp_file_addr)) < 0) {
        LogError("%s CopyString to tmp_file_addr failed\n", __func__);
        goto end;
    }

    pcm_parser_free(parser);
    parser->seek_pos_ms = 0;
    parser->src_sample_rate_in_Hz = src_sample_rate;
    parser->src_nb_channels = src_nb_channels;
    parser->dst_sample_rate_in_Hz = dst_sample_rate;
    parser->dst_nb_channels = dst_nb_channels;

    // Allocate buffer for audio fifo
    parser->pcm_fifo = fifo_create(sizeof(int16_t));
    if (!parser->pcm_fifo) {
        LogError("%s Could not allocate pcm FIFO\n", __func__);
        ret = AEERROR_NOMEM;
        goto end;
    }

    parser->max_src_buffer_size = MAX_NB_SAMPLES;
    parser->src_buffer = (short *)calloc(sizeof(short), parser->max_src_buffer_size);
    if (!parser->src_buffer) {
        LogError("%s calloc src_buffer failed.\n", __func__);
        ret = AEERROR_NOMEM;
        goto end;
    }

    if (src_nb_channels != dst_nb_channels) {
        if (src_nb_channels == 1) {
            parser->max_dst_buffer_size = parser->max_src_buffer_size << 1;
        } else if (src_nb_channels == 2) {
            parser->max_dst_buffer_size = parser->max_src_buffer_size >> 1;
        } else {
            LogError("%s unsupport src_nb_channels %d.\n", __func__, src_nb_channels);
            ret = -1;
            goto end;
        }
        parser->dst_buffer = (short *)calloc(sizeof(short), parser->max_dst_buffer_size);
        if (!parser->dst_buffer) {
            LogError("%s calloc dst_buffer failed.\n", __func__);
            ret = AEERROR_NOMEM;
            goto end;
        }
    }

    if ((ret = CopyString(tmp_file_addr, &parser->file_addr)) < 0) {
        LogError("%s CopyString failed\n", __func__);
        goto end;
    }

    if ((ret = ae_open_file(&parser->reader, parser->file_addr, false)) < 0) {
	LogError("%s read file_addr %s failed\n", __func__, parser->file_addr);
	goto end;
    }
    fseek(parser->reader, 0, SEEK_END);
    parser->file_size = ftell(parser->reader);
    fseek(parser->reader, 0, SEEK_SET);

    if (tmp_file_addr) {
        av_freep(&tmp_file_addr);
    }
    return 0;
end:
    if (tmp_file_addr) {
        av_freep(tmp_file_addr);
    }
    pcm_parser_free(parser);
    return ret;
}

void pcm_parser_free(PcmParser *parser) {
    LogInfo("%s\n", __func__);
    if (NULL == parser)
        return;

    if (parser->pcm_fifo) {
        fifo_delete(&parser->pcm_fifo);
    }
    if (parser->src_buffer) {
        av_freep(&parser->src_buffer);
    }
    if (parser->dst_buffer) {
        av_freep(&parser->dst_buffer);
    }
    if (parser->file_addr) {
        av_freep(&parser->file_addr);
    }
    if (parser->reader) {
        fclose(parser->reader);
        parser->reader = NULL;
    }
}

void pcm_parser_freep(PcmParser **parser) {
    LogInfo("%s\n", __func__);
    if (!parser || !*parser)
        return;

    pcm_parser_free(*parser);
    av_freep(parser);
}

int pcm_parser_get_pcm_frame(PcmParser *parser,
        short *buffer, int buffer_size_in_short, bool loop) {
    int ret = -1;
    if (!parser || !buffer || buffer_size_in_short < 0)
        return ret;

    while (fifo_occupancy(parser->pcm_fifo) < (size_t) buffer_size_in_short) {
	ret = write_fifo(parser);
	if (ret < 0) {
	    if (loop && ret == PCM_FILE_EOF) {
	        init_parser(parser, parser->file_addr, parser->src_sample_rate_in_Hz,
	            parser->src_nb_channels, parser->dst_sample_rate_in_Hz, parser->dst_nb_channels);
	    } else if (0 < fifo_occupancy(parser->pcm_fifo)) {
	        break;
	    } else {
	        goto end;
	    }
	}
    }

    return fifo_read(parser->pcm_fifo, buffer, buffer_size_in_short);
end:
    return ret;
}

int pcm_parser_seekTo(PcmParser *parser, int seek_pos_ms) {
    LogInfo("%s seek_pos_ms %d\n", __func__, seek_pos_ms);
    if (NULL == parser)
        return -1;

    parser->seek_pos_ms = seek_pos_ms < 0 ? 0 : seek_pos_ms;
    if (parser->pcm_fifo) fifo_clear(parser->pcm_fifo);

    //The offset needs to be a multiple of 2, because the pcm data is 16-bit.
    //The size of seek is in pcm data.
    int64_t offset = align(2 * ((int64_t) parser->seek_pos_ms * parser->src_nb_channels) *
        (parser->src_sample_rate_in_Hz / (float) 1000), 2);
    LogInfo("%s fseek offset %"PRId64".\n", __func__, offset);
    int ret = fseek(parser->reader, offset, SEEK_SET);
    return ret;
}

PcmParser *pcm_parser_create(const char *file_addr, int src_sample_rate,
    int src_nb_channels, int dst_sample_rate, int dst_nb_channels) {
    LogInfo("%s.\n", __func__);
    int ret = -1;
    if (NULL == file_addr) {
        LogError("%s file_addr is NULL.\n", __func__);
        return NULL;
    }

    if (src_sample_rate != dst_sample_rate) {
        LogError("%s not support pcm resampling.\n", __func__);
        return NULL;
    }

    PcmParser *parser = (PcmParser *)calloc(1, sizeof(PcmParser));
    if (NULL == parser) {
        LogError("%s Could not allocate PcmParser.\n", __func__);
        goto end;
    }

    if ((ret = init_parser(parser, file_addr, src_sample_rate, src_nb_channels,
        dst_sample_rate, dst_nb_channels)) < 0) {
        LogError("%s init_parser failed\n", __func__);
        goto end;
    }

    return parser;
end:
    if (parser) {
        pcm_parser_freep(&parser);
    }
    return NULL;
}
