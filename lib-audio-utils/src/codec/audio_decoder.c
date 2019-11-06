#if defined(__ANDROID__) || defined (__linux__)
#include "audio_decoder.h"
#include "log.h"
#include "error_def.h"

#define milliseconds_to_fftime(ms) (av_rescale(ms, AV_TIME_BASE, 1000))

static inline void free_input_media_context(AVFormatContext **fmt_ctx,
                                         AVCodecContext **dec_ctx) {
    LogInfo("%s.\n", __func__);
    if (*fmt_ctx) {
        avformat_close_input(fmt_ctx);
        *fmt_ctx = NULL;
    }
    if (*dec_ctx) {
        avcodec_free_context(dec_ctx);
        *dec_ctx = NULL;
    }
}

static int get_frame_from_fifo(AudioDecoder *decoder, short *buffer,
                                    const int buffer_size_in_short) {
    int ret = -1;
    if (NULL == decoder)
        return ret;

    int nb_samples = buffer_size_in_short / decoder->dst_nb_channels;
    if (nb_samples > decoder->max_nb_copy_samples) {
        decoder->max_nb_copy_samples = nb_samples;
        ret = AllocateSampleBuffer(&(decoder->copy_buffer), decoder->dst_nb_channels,
                                   decoder->max_nb_copy_samples, AV_SAMPLE_FMT_S16);
        if (ret < 0) goto end;
    }

    memset(buffer, 0, sizeof(short) * buffer_size_in_short);
    ret = AudioFifoGet(decoder->audio_fifo, nb_samples, (void**)decoder->copy_buffer);
    if (ret < 0) goto end;

    ret = ret * decoder->dst_nb_channels;
    memcpy(buffer, decoder->copy_buffer[0], sizeof(short) * ret);
    return buffer_size_in_short;

end:
    return ret;
}

static int resample_audio(AudioDecoder *decoder) {
    int ret = -1;
    if (NULL == decoder)
        return ret;

    decoder->dst_nb_samples = swr_get_out_samples(decoder->swr_ctx, decoder->audio_frame->nb_samples);
    if (decoder->dst_nb_samples > decoder->max_dst_nb_samples) {
        decoder->max_dst_nb_samples = decoder->dst_nb_samples;
        ret = AllocateSampleBuffer(&(decoder->dst_data), decoder->dst_nb_channels,
                               decoder->max_dst_nb_samples, AV_SAMPLE_FMT_S16);
        if (ret < 0) {
            LogError("%s av_samples_alloc error, error code = %d.\n", __func__, ret);
            goto end;
        }
    }
    // Convert to destination format
    ret = decoder->dst_nb_samples =
        swr_convert(decoder->swr_ctx, decoder->dst_data, decoder->dst_nb_samples,
                    (const uint8_t **)decoder->audio_frame->data,
                    decoder->audio_frame->nb_samples);
    if (ret < 0) {
        LogError("%s swr_convert error, error code = %d.\n", __func__, ret);
        goto end;
    }

end:
    return ret < 0 ? ret : 0;
}

static int read_audio_packet(AudioDecoder *decoder, AVPacket *pkt) {
    int ret = -1;
    if (NULL == decoder)
        return ret;

    InitPacket(pkt);

    if (decoder->seek_req) {
        AVStream *audio_stream = decoder->fmt_ctx->streams[decoder->audio_stream_index];
        int64_t stream_start_pos = (audio_stream->start_time != AV_NOPTS_VALUE ? audio_stream->start_time : 0);
        int64_t seek_pos = milliseconds_to_fftime(decoder->seek_pos_ms);
        ret = avformat_seek_file(decoder->fmt_ctx, -1, INT64_MIN,
            seek_pos + stream_start_pos, INT64_MAX, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            LogError("%s: error while seeking.\n", __func__);
            return ret;
        } else {
            LogInfo("%s: avformat_seek_file success.\n", __func__);
            AudioFifoReset(decoder->audio_fifo);
        }
        decoder->seek_pos_ms = 0;
        decoder->seek_req = false;
    }

    while (1) {
        ret = av_read_frame(decoder->fmt_ctx, pkt);
        if (ret < 0) {
            if (AVERROR_EOF == ret) {
                LogWarning("%s Audio file is end of file.\n", __func__);
            } else {
                LogError("%s av_read_frame error, error code = %d.\n", __func__, ret);
            }
            break;
        }
        if (decoder->audio_stream_index == pkt->stream_index) break;
        av_packet_unref(pkt);
    }

    return ret;
}

static int decode_audio_frame(AudioDecoder *decoder) {
    int ret = -1;
    if (NULL == decoder)
        return ret;

    AVPacket input_pkt;

    if (!decoder->dec_ctx) return kNullPointError;

    ret = read_audio_packet(decoder, &input_pkt);
    if (ret < 0) goto end;

    // If the packet is audio packet, decode and convert it to frame, then put
    // the frame into audio_fifo.
    if (decoder->audio_stream_index == input_pkt.stream_index) {
        ret = avcodec_send_packet(decoder->dec_ctx, &input_pkt);
        if (ret < 0) {
            LogError("%s Error submitting the packet to the decoder, error code = %d.\n", __func__, ret);
            goto end;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(decoder->dec_ctx, decoder->audio_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                ret = 0;
                goto end;
            } else if (ret < 0) {
                LogError("%s Error during avcodec_receive_frame, error code = %d.\n", __func__, ret);
                goto end;
            }

            if (decoder->swr_ctx) {
                ret = resample_audio(decoder);
                if (ret < 0) break;
                ret = AudioFifoPut(decoder->audio_fifo, decoder->dst_nb_samples,
                                   (void **)decoder->dst_data);
                if (ret < 0) goto end;
            } else {
                ret =
                    AudioFifoPut(decoder->audio_fifo, decoder->audio_frame->nb_samples,
                                 (void **)decoder->audio_frame->data);
                if (ret < 0) goto end;
            }
        }
    }

end:
    av_packet_unref(&input_pkt);
    return ret < 0 ? ret : 0;
}

static int open_audio_file(AudioDecoder *decoder, const char *file_addr) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    AVCodecContext *avctx = NULL;
    if (NULL == decoder)
        return ret;

    ret = CopyString(file_addr, &(decoder->file_addr));
    if (ret < 0) goto end;

    ret = OpenInputMediaFile(&(decoder->fmt_ctx), decoder->file_addr);
    if (ret < 0) goto end;

    ret = decoder->audio_stream_index = FindBestStream(decoder->fmt_ctx, AVMEDIA_TYPE_AUDIO);
    if (ret < 0) goto end;

    ret = FindAndOpenDecoder(decoder->fmt_ctx, &(decoder->dec_ctx), decoder->audio_stream_index);
    if (ret < 0) goto end;

    ret = InitResampler(decoder->dec_ctx->channels, decoder->dst_nb_channels,
                        decoder->dec_ctx->sample_rate, decoder->dst_sample_rate_in_Hz,
                        decoder->dec_ctx->sample_fmt, AV_SAMPLE_FMT_S16,
                        &(decoder->swr_ctx));
    if (ret < 0) goto end;

    return 0;

end:
    free_input_media_context(&(decoder->fmt_ctx), &avctx);
    return ret;
}

static int init_decoder(AudioDecoder *decoder, const int sample_rate_in_Hz,
        const int nb_channels) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if (NULL == decoder)
        return ret;

    ret = CheckSampleRateAndChannels(sample_rate_in_Hz, nb_channels);
    if (ret < 0) goto end;

    xm_audio_decoder_free(decoder);
    decoder->dst_sample_rate_in_Hz = sample_rate_in_Hz;
    decoder->dst_nb_channels = nb_channels;
    decoder->max_nb_copy_samples = MAX_NB_SAMPLES;
    decoder->max_dst_nb_samples = MAX_NB_SAMPLES;
    decoder->dst_nb_samples = MAX_NB_SAMPLES;
    decoder->audio_stream_index = -1;
    decoder->seek_pos_ms = 0;
    decoder->seek_req = false;

    // Allocate sample buffer for resampler
    ret = AllocateSampleBuffer(&(decoder->dst_data), nb_channels,
                               decoder->max_dst_nb_samples, AV_SAMPLE_FMT_S16);
    if (ret < 0) goto end;

    // Allocate sample buffer for copy_buffer
    ret = AllocateSampleBuffer(&(decoder->copy_buffer), nb_channels,
                               decoder->max_nb_copy_samples, AV_SAMPLE_FMT_S16);
    if (ret < 0) goto end;

    // Allocate buffer for audio frame
    decoder->audio_frame = av_frame_alloc();
    if (NULL == decoder->audio_frame) {
        LogError("%s Could not allocate input audio frame\n", __func__);
        ret = AVERROR(ENOMEM);
        goto end;
    }

    // Allocate buffer for audio fifo
    decoder->audio_fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, nb_channels, 1);
    if (NULL == decoder->audio_fifo) {
        LogError("%s Could not allocate audio FIFO\n", __func__);
        ret = AVERROR(ENOMEM);
        goto end;
    }
    return 0;

end:
    xm_audio_decoder_free(decoder);
    return ret;
}

static int reset_decoder(AudioDecoder *decoder, const char *file_addr,
        int sample_rate, int channels) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    char *tmp_file_addr = NULL;
    if (NULL == decoder || NULL == file_addr)
        return ret;

    if ((ret = CopyString(file_addr, &tmp_file_addr)) < 0) {
        LogError("%s CopyString failed\n", __func__);
        goto end;
    }

    if ((ret = init_decoder(decoder, sample_rate, channels)) < 0) {
        LogError("%s init_decoder failed\n", __func__);
        goto end;
    }

    if ((ret = open_audio_file(decoder, tmp_file_addr)) < 0) {
        LogError("%s open_audio_file failed\n", __func__);
        goto end;
    }

end:
    if (tmp_file_addr) {
        free(tmp_file_addr);
    }
    return ret;
}

void xm_audio_decoder_free(AudioDecoder *decoder) {
    LogInfo("%s\n", __func__);
    if (NULL == decoder)
        return;

    if (decoder->audio_fifo) {
        av_audio_fifo_free(decoder->audio_fifo);
        decoder->audio_fifo = NULL;
    }
    if (decoder->copy_buffer) {
        av_freep(&(decoder->copy_buffer[0]));
        av_freep(&(decoder->copy_buffer));
    }
    free_input_media_context(&(decoder->fmt_ctx), &(decoder->dec_ctx));
    if (decoder->audio_frame) {
        av_frame_free(&(decoder->audio_frame));
        decoder->audio_frame = NULL;
    }
    if (decoder->swr_ctx) {
        swr_free(&(decoder->swr_ctx));
        decoder->swr_ctx = NULL;
    }
    if (decoder->dst_data) {
        av_freep(&(decoder->dst_data[0]));
        av_freep(&(decoder->dst_data));
    }
    if (decoder->file_addr) {
        av_freep(&(decoder->file_addr));
        decoder->file_addr = NULL;
    }
}

void xm_audio_decoder_freep(AudioDecoder **decoder) {
    LogInfo("%s\n", __func__);
    if (NULL == decoder || NULL == *decoder)
        return;

    xm_audio_decoder_free(*decoder);
    av_freep(decoder);
}

int xm_audio_decoder_get_decoded_frame(AudioDecoder *decoder, short *buffer,
                                   const int buffer_size_in_short, bool loop) {
    int ret = -1;
    if (NULL == decoder)
        return ret;

    while (av_audio_fifo_size(decoder->audio_fifo) * decoder->dst_nb_channels <
           buffer_size_in_short) {
        ret = decode_audio_frame(decoder);
        if (ret < 0) {
            if (ret == AVERROR_EOF && loop) {
                if ((ret = reset_decoder(decoder, decoder->file_addr,
                        decoder->dst_sample_rate_in_Hz, decoder->dst_nb_channels)) < 0) {
                    LogError("%s reset_decoder failed\n", __func__);
                    goto end;
                }
            } else if (ret == AVERROR_EOF && 0 != av_audio_fifo_size(decoder->audio_fifo)) {
                break;
            } else {
                goto end;
            }
        }
    }
    return get_frame_from_fifo(decoder, buffer, buffer_size_in_short);

end:
    return ret;
}

void xm_audio_decoder_seekTo(AudioDecoder *decoder, int seek_pos_ms) {
    LogInfo("%s\n", __func__);
    if (NULL == decoder)
        return;

    decoder->seek_req = true;
    decoder->seek_pos_ms = seek_pos_ms;
    AudioFifoReset(decoder->audio_fifo);
}

AudioDecoder *xm_audio_decoder_create(const char *file_addr,
        const int sample_rate, const int channels) {
    LogInfo("%s.\n", __func__);
    int ret = -1;
    if (NULL == file_addr) {
        LogError("%s file_addr is NULL.\n", __func__);
        return NULL;
    }

    AudioDecoder *decoder = (AudioDecoder *)calloc(1, sizeof(AudioDecoder));
    if (NULL == decoder) {
        LogError("%s Could not allocate AudioDecoder\n", __func__);
        goto end;
    }

    if ((ret = reset_decoder(decoder, file_addr, sample_rate, channels)) < 0) {
        LogError("%s reset_decoder failed\n", __func__);
        goto end;
    }

    return decoder;
end:
    if (decoder) {
        xm_audio_decoder_freep(&decoder);
    }
    return NULL;
}
#endif
