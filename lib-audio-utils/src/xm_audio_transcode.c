#include "xm_audio_transcode.h"
#include "codec/audio_decoder_factory.h"
#include "codec/audio_muxer.h"
#include "error_def.h"
#include "log.h"
#include "js_callback.h"

struct XmAudioTranscoder {
    volatile int status;
    volatile bool abort;
    int progress;
    char *js_progress_callback;
    pthread_mutex_t mutex;
};

extern void RegisterFFmpeg();

static int chk_st_l(int state)
{
    if (state == TRANSCODER_STATE_INITIALIZED ||
        state == TRANSCODER_STATE_COMPLETED ||
        state == TRANSCODER_STATE_ERROR) {
        return 0;
    }

    LogError("%s state(%d) is invalid.\n", __func__, state);
    LogError("%s expecting status is INITIALIZED or \
        state is COMPLETED.\n", __func__);
    return -1;
}

void xm_audio_transcoder_freep(XmAudioTranscoder *self)
{
    LogInfo("%s\n", __func__);
    if (NULL == self)
        return;

    if (self->js_progress_callback) {
        free(self->js_progress_callback);
        self->js_progress_callback = NULL;
    }
    pthread_mutex_destroy(&self->mutex);
    free(self);
}

void xm_audio_transcoder_stop(XmAudioTranscoder *self)
{
    LogInfo("%s\n", __func__);
    if (NULL == self)
        return;

    pthread_mutex_lock(&self->mutex);
    self->abort = true;
    pthread_mutex_unlock(&self->mutex);
}

int xm_audio_transcoder_get_progress(XmAudioTranscoder *self)
{
    int ret = -1;
    if (NULL == self)
        return ret;

    pthread_mutex_lock(&self->mutex);
    ret = self->progress;
    pthread_mutex_unlock(&self->mutex);
    return ret;
}

int xm_audio_transcoder_set_progress_callback(
    XmAudioTranscoder *self, const char *callback)
{
    if (!self || !callback)
        return -1;

    if (self->js_progress_callback) {
        free(self->js_progress_callback);
        self->js_progress_callback = NULL;
    }
    self->js_progress_callback = av_strdup(callback);
    return 0;
}

int xm_audio_transcoder_start(XmAudioTranscoder *self,
                              const char *in_audio_path, const char *out_m4a_path)
{
    LogInfo("%s\n", __func__);
    int ret = -1;
    short *buffer = NULL;
    if (!self || !in_audio_path || !out_m4a_path) {
        return ret;
    }

    if (chk_st_l(self->status) < 0) {
        return AEERROR_INVALID_STATE;
    }

    pthread_mutex_lock(&self->mutex);
    self->status = TRANSCODER_STATE_STARTED;
    self->abort = false;
    self->progress = 0;
    pthread_mutex_unlock(&self->mutex);

    char *in_file_addr = av_strdup(in_audio_path);
    char *out_file_addr = av_strdup(out_m4a_path);
    int buffer_size_in_short = 1024, channels = 0;
    int sample_rate = 0, duration, audio_stream_index = -1;
    AVFormatContext *fmt_ctx = NULL;

    ret = OpenInputMediaFile(&fmt_ctx, in_file_addr);
    if (ret < 0) {
        LogError("%s OpenInputMediaFile failed.\n", __func__);
        goto end;
    }

    ret = audio_stream_index = FindBestStream(fmt_ctx, AVMEDIA_TYPE_AUDIO);
    if (ret < 0) {
        LogError("%s FindBestStream failed.\n", __func__);
        goto end;
    }

    AVStream *stream = fmt_ctx->streams[audio_stream_index];
    if (stream && stream->codecpar) {
        channels = stream->codecpar->channels;
        sample_rate = stream->codecpar->sample_rate;
        if (stream->duration != AV_NOPTS_VALUE) {
            duration = av_rescale_q(stream->duration,
                                    stream->time_base, AV_TIME_BASE_Q) / 1000;
        } else {
            duration = av_rescale(fmt_ctx->duration, 1000, AV_TIME_BASE);
        }
    } else {
        LogError("%s get sample_rate and channels failed.\n", __func__);
        ret = -1;
        goto end;
    }

    buffer = (short *)calloc(sizeof(short), buffer_size_in_short);
    if (!buffer) {
        LogError("%s calloc buffer failed.\n", __func__);
        ret = AEERROR_NOMEM;
        goto end;
    }

    IAudioDecoder *decoder = audio_decoder_create(in_file_addr, 0, 0,
                             sample_rate, channels, 1.0f, DECODER_FFMPEG);
    if (decoder == NULL) {
        LogError("audio_decoder_create failed\n");
        ret = AEERROR_NOMEM;
        goto end;
    }

    MuxerConfig config;
    config.src_sample_rate_in_Hz = sample_rate;
    config.src_nb_channels = channels;
    config.dst_sample_rate_in_Hz = sample_rate;
    config.dst_nb_channels = channels;
    config.mime = MIME_AUDIO_AAC;
    config.muxer_name = MUXER_AUDIO_MP4;
    config.output_filename = out_file_addr;
    config.src_sample_fmt = AV_SAMPLE_FMT_S16;
    config.codec_id = AV_CODEC_ID_AAC;
    config.encoder_type = ENCODER_FFMPEG;
    AudioMuxer *muxer = muxer_create(&config);
    if (muxer == NULL) {
        LogError("muxer_create failed\n");
        ret = AEERROR_NOMEM;
        goto end;
    }

    uint32_t cur_size = 0;
    uint32_t total_size = 2 * sample_rate * channels
                          * ((float)duration / 1000);
    while (!self->abort) {
        int progress = ((float)cur_size / total_size) * 100;
        if (progress > self->progress) {
            LogInfo("%s progress %d.\n", __func__, progress);
            js_progress_callback(self->js_progress_callback,
                                 progress);
        }

        pthread_mutex_lock(&self->mutex);
        self->progress = progress;
        pthread_mutex_unlock(&self->mutex);

        ret = IAudioDecoder_get_pcm_frame(decoder, buffer,
                                          buffer_size_in_short, false);
        if (ret < 0) {
            LogInfo("ret < 0, error or EOF, exit\n");
            break;
        }
        cur_size += ret * sizeof(short);

        ret = muxer_write_audio_frame(muxer, buffer, ret);
        if (ret < 0) {
            LogError("muxer_write_audio_frame failed\n");
            break;
        }
    }

    ret = 0;
end:
    if (in_file_addr) free(in_file_addr);
    if (out_file_addr) free(out_file_addr);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
    if (buffer) {
        free(buffer);
        buffer = NULL;
    }

    if (decoder) {
        IAudioDecoder_freep(&decoder);
    }

    muxer_stop(muxer);
    muxer_freep(&muxer);

    pthread_mutex_lock(&self->mutex);
    if (ret < 0)
        self->status = TRANSCODER_STATE_ERROR;
    else
        self->status = TRANSCODER_STATE_COMPLETED;
    pthread_mutex_unlock(&self->mutex);
    return ret;
}

XmAudioTranscoder *xm_audio_transcoder_create()
{
    XmAudioTranscoder *self =
        (XmAudioTranscoder *)calloc(1, sizeof(XmAudioTranscoder));
    if (NULL == self) {
        LogError("%s alloc XmAudioTranscoder failed.\n", __func__);
        return NULL;
    }

    pthread_mutex_init(&self->mutex, NULL);
    self->status = TRANSCODER_STATE_INITIALIZED;
    RegisterFFmpeg();
    return self;
}
