#include "audio_decoder_factory.h"
#include "codec/ffmpeg_decoder.h"
#include "pcm_decoder.h"
#include "codec/ffmpeg_utils.h"

IAudioDecoder *audio_decoder_create(const char *file_addr,
    int src_sample_rate, int src_nb_channels, int dst_sample_rate,
    int dst_nb_channels)
{
    AVFormatContext *fmt_ctx = NULL;
    enum DecoderType decoder_type = DECODER_NONE;
    IAudioDecoder *decoder = NULL;

    int ret = OpenInputMediaFile(&fmt_ctx, file_addr);
    if (ret < 0) {
        decoder_type = DECODER_PCM;
    } else {
        decoder_type =DECODER_FFMPEG;
    }

    switch(decoder_type) {
        case DECODER_PCM:
            decoder = PcmDecoder_create(file_addr, src_sample_rate,
                            src_nb_channels, src_sample_rate, dst_nb_channels);
        break;
        case DECODER_FFMPEG:
            decoder = FFmpegDecoder_create(file_addr, dst_sample_rate,
                            dst_nb_channels);
        break;
        default:
        break;
    }

    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = NULL;
    }
    return decoder;
}

