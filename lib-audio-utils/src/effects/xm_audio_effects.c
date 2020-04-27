#include "xm_audio_effects.h"
#include <assert.h>
#include <string.h>
#include "log.h"
#include "tools/util.h"
#include "json/json_parse.h"
#include "effects/voice_effect.h"
#include "wave/wav_dec.h"

#define DEFAULT_SAMPLE_RATE 44100
#define DEFAULT_CHANNEL_NUMBER 1

static void voice_effects_free(VoiceEffects *voice) {
    LogInfo("%s\n", __func__);
    if (!voice) {
        return;
    }

    for (short i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (voice->effects[i]) {
            free_effect(voice->effects[i]);
            voice->effects[i] = NULL;
        }
    }
}

static void voice_effects_info_free(VoiceEffects *voice) {
    LogInfo("%s\n", __func__);
    if (!voice) {
        return;
    }

    for (short i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (voice->effects_info[i]) {
            free(voice->effects_info[i]);
            voice->effects_info[i] = NULL;
        }
    }
}

static int voice_effects_init(XmEffectContext *ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx)
        return -1;

    VoiceEffects *voice = &ctx->voice_effects;
    voice_effects_free(voice);

    for (int i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (NULL == voice->effects_info[i]) continue;

        switch (i) {
            case NoiseSuppression:
                voice->effects[NoiseSuppression] = create_effect(find_effect("noise_suppression"),
                        ctx->dst_sample_rate, ctx->dst_channels);
                init_effect(voice->effects[NoiseSuppression], 0, NULL);
                set_effect(voice->effects[NoiseSuppression], "Switch", voice->effects_info[i], 0);
                break;
            case Beautify:
                voice->effects[Beautify] = create_effect(find_effect("beautify"),
                        ctx->dst_sample_rate, ctx->dst_channels);
                init_effect(voice->effects[Beautify], 0, NULL);
                set_effect(voice->effects[Beautify], "mode", voice->effects_info[i], 0);
                break;
            case Reverb:
                voice->effects[Reverb] = create_effect(find_effect("reverb"),
                        ctx->dst_sample_rate, ctx->dst_channels);
                init_effect(voice->effects[Reverb], 0, NULL);
                set_effect(voice->effects[Reverb], "mode", voice->effects_info[i], 0);
                break;
            case VolumeLimiter:
                voice->effects[VolumeLimiter] = create_effect(find_effect("limiter"),
                        ctx->dst_sample_rate, ctx->dst_channels);
                init_effect(voice->effects[VolumeLimiter], 0, NULL);
                set_effect(voice->effects[VolumeLimiter], "Switch", voice->effects_info[i], 0);
                break;
            default:
                LogWarning("%s unsupported effect %s\n", __func__, voice->effects_info[i]);
                break;
        }
    }

    return 0;
}

static IAudioDecoder *open_record_source_decoder(
        AudioRecordSource *source, int dst_sample_rate,
        int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!source || !source->file_path)
        return NULL;

    IAudioDecoder_freep(&(source->decoder));

    source->decoder = audio_decoder_create(source->file_path,
        source->sample_rate, source->nb_channels, dst_sample_rate,
        dst_channels, source->volume, source->decoder_type);
    if (source->decoder == NULL) {
        LogError("%s record audio_decoder_create failed.\n", __func__);
        return NULL;
    }

    if (IAudioDecoder_set_crop_pos(source->decoder,
        source->crop_start_time_ms, source->crop_end_time_ms) < 0) {
        LogError("%s IAudioDecoder_set_crop_pos failed.\n", __func__);
        IAudioDecoder_freep(&(source->decoder));
        return NULL;
    }

    IAudioDecoder_seekTo(source->decoder, seek_time_ms);
    return source->decoder;
}

static int update_record_source(AudioRecordSourceQueue *queue,
        AudioRecordSource *source, int dst_sample_rate, int dst_channels) {
    int ret = -1;
    if (!queue || !source)
        return ret;

    while (AudioRecordSourceQueue_size(queue) > 0) {
        AudioRecordSource_free(source);
        if (AudioRecordSourceQueue_get(queue, source) > 0) {
            source->decoder = open_record_source_decoder(source,
                        dst_sample_rate, dst_channels, 0);
            if (!source->decoder)
            {
                LogError("%s open decoder failed, file_path: %s.\n", __func__, source->file_path);
                ret = AEERROR_NOMEM;
            } else {
                ret = 0;
                break;
            }
        }
    }
    return ret;
}

static int record_source_seekTo(AudioRecordSourceQueue *queue,
        AudioRecordSource *source, int dst_sample_rate, int dst_channels, int seek_time_ms) {
    LogInfo("%s\n", __func__);
    if (!queue || !source || (AudioRecordSourceQueue_size(queue) <= 0))
        return -1;

    int source_seek_time = 0;
    bool find_source = false;
    while ((AudioRecordSourceQueue_size(queue) > 0)) {
        AudioRecordSource_free(source);
        if (AudioRecordSourceQueue_get(queue, source) > 0) {
            if (source->start_time_ms <= seek_time_ms) {
                if (source->end_time_ms <= seek_time_ms) {
                    source_seek_time = 0;
                } else {
                    source_seek_time = seek_time_ms - source->start_time_ms;
                    find_source = true;
                    break;
                }
            } else {
                source_seek_time = 0;
                find_source = true;
                break;
            }
        }
    }

    if (find_source) {
        source->decoder = open_record_source_decoder(source,
                dst_sample_rate, dst_channels, 0);
        if (!source->decoder)
        {
            LogError("%s open decoder failed, file_path: %s.\n", __func__, source->file_path);
            return AEERROR_NOMEM;
        }

        source_seek_time = source_seek_time < 0 ? 0 :
            (source_seek_time > source->decoder->duration_ms
            ? source->decoder->duration_ms : source_seek_time);
        return IAudioDecoder_seekTo(source->decoder, source_seek_time);
    } else {
        AudioRecordSource_free(source);
    }
    return 0;
}

static void ae_free(XmEffectContext *ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx)
        return;

    if (ctx->voice_effects.record) {
        AudioRecordSource_freep(&(ctx->voice_effects.record));
    }
    if (ctx->voice_effects.recordQueue) {
        AudioRecordSourceQueue_freep(&(ctx->voice_effects.recordQueue));
    }

    voice_effects_info_free(&ctx->voice_effects);
    voice_effects_free(&ctx->voice_effects);

    if (ctx->in_config_path) {
        free(ctx->in_config_path);
        ctx->in_config_path = NULL;
    }

    if (ctx->audio_fifo) {
        fifo_delete(&ctx->audio_fifo);
    }
}

static int ae_chkst_l(int ae_state)
{
    if (ae_state == AE_STATE_INITIALIZED) {
        return 0;
    }

    LogError("%s audio effect state(%d) is invalid.\n", __func__, ae_state);
    LogError("%s expecting ae_state == AE_STATE_INITIALIZED(1).\n", __func__);
    return -1;
}

static void ae_abort_l(XmEffectContext *ctx)
{
    if(!ctx)
        return;

    pthread_mutex_lock(&ctx->mutex);
    ctx->abort= true;
    pthread_mutex_unlock(&ctx->mutex);
}

static void ae_reset_l(XmEffectContext *ctx)
{
    if(!ctx)
        return;

    ae_abort_l(ctx);
    ae_free(ctx);

    memset(ctx->voice_effects.effects_info, 0, MAX_NB_EFFECTS * sizeof(char *));
    memset(ctx->voice_effects.effects, 0, MAX_NB_EFFECTS * sizeof(EffectContext *));
    memset(ctx->buffer, 0, MAX_NB_SAMPLES * sizeof(*(ctx->buffer)));
    pthread_mutex_lock(&ctx->mutex);
    ctx->abort= false;
    ctx->progress = 0;
    ctx->flush = false;
    pthread_mutex_unlock(&ctx->mutex);
}

static void flush(XmEffectContext *ctx) {
    LogInfo("%s start.\n", __func__);
    if (!ctx)
        return;

    VoiceEffects *voice_effects = &ctx->voice_effects;
    while (1) {
        int receive_len = 0;
        for (int i = 0; i < MAX_NB_EFFECTS; ++i) {
            if (NULL == voice_effects->effects[i]) {
                continue;
            }

            bool is_last_effect = true;
            receive_len = receive_samples(voice_effects->effects[i], ctx->buffer, MAX_NB_SAMPLES);
            while (receive_len > 0) {
                for (short j = i + 1; j < MAX_NB_EFFECTS; ++j) {
                    if (NULL != voice_effects->effects[j]) {
                        receive_len = send_samples(voice_effects->effects[j], ctx->buffer, receive_len);
                        if (receive_len < 0) {
                            LogError("%s send_samples to the next effect failed\n", __func__);
                            goto end;
                        }
                        is_last_effect = false;
                        receive_len = receive_samples(voice_effects->effects[i], ctx->buffer, MAX_NB_SAMPLES);
                        break;
                    }
                }
                if (is_last_effect) break;
            }
        }

        if (receive_len > 0) {
            int ret = fifo_write(ctx->audio_fifo, ctx->buffer, receive_len);
            if (ret < 0) goto end;
        } else {
            goto end;
        }
    }
end:
    LogInfo("%s end.\n", __func__);
    ctx->flush = true;
    return;
}

static int add_effects(XmEffectContext *ctx, short *buffer, int buffer_len) {
    int ret = -1;
    if (!ctx || !buffer || buffer_len <= 0) return -1;

    VoiceEffects *voice_effects = &ctx->voice_effects;
    bool find_valid_effect = false;
    for (int i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (NULL != voice_effects->effects[i]) {
            if(send_samples(voice_effects->effects[i], buffer,
                    buffer_len) < 0) {
                LogError("%s send_samples to the first effect failed\n", __func__);
                ret = -1;
                return ret;
            }
            find_valid_effect = true;
            break;
        }
    }
    if (!find_valid_effect) return buffer_len;

    int receive_len = 0;
    for (int i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (NULL == voice_effects->effects[i]) {
            continue;
        }

        bool is_last_effect = true;
        receive_len = receive_samples(voice_effects->effects[i], buffer, MAX_NB_SAMPLES);
        while (receive_len > 0) {
            for (short j = i + 1; j < MAX_NB_EFFECTS; ++j) {
                if (NULL != voice_effects->effects[j]) {
                    receive_len = send_samples(voice_effects->effects[j], buffer, receive_len);
                    if (receive_len < 0) {
                        LogError("%s send_samples to the next effect failed\n", __func__);
                        ret = -1;
                        return ret;
                    }
                    is_last_effect = false;
                    receive_len = receive_samples(voice_effects->effects[i], buffer, MAX_NB_SAMPLES);
                    break;
                }
            }
            if (is_last_effect) break;
        }
    }

    ret = receive_len;
    return ret;
}

static int read_pcm_frame(XmEffectContext *ctx, short *buffer) {
    int ret = -1;
    if (!ctx || !buffer || !(ctx->voice_effects.record)
        || !(ctx->voice_effects.recordQueue)) return -1;

    int cur_position = ctx->seek_time_ms +
        calculation_duration_ms(ctx->cur_size, ctx->dst_bits_per_sample/8,
        ctx->dst_channels, ctx->dst_sample_rate);
    if (cur_position >= ctx->duration_ms) {
        ret = PCM_FILE_EOF;
        return ret;
    }

    int record_start_time = 0;
    int record_end_time = 0;
    IAudioDecoder *record_decoder = NULL;
    if ((!ctx->voice_effects.record->decoder) &&
            (AudioRecordSourceQueue_size(ctx->voice_effects.recordQueue) > 0)) {
        update_record_source(ctx->voice_effects.recordQueue,
            ctx->voice_effects.record, ctx->dst_sample_rate, ctx->dst_channels);
        record_decoder = ctx->voice_effects.record->decoder;
        if (record_decoder) {
            if (ctx->dst_sample_rate != record_decoder->out_sample_rate
                    || ctx->dst_channels != record_decoder->out_nb_channels
                    || ctx->dst_bits_per_sample != record_decoder->out_bits_per_sample) {
                LogError("%s effects dst_sample_rate != out_sample_rate or "
                            "dst_channels != out_nb_channels or "
                            "dst_bits_per_sample != out_bits_per_sample.\n", __func__);
                return -1;
            }
        }
    }

    record_start_time = ctx->voice_effects.record->start_time_ms;
    record_decoder = ctx->voice_effects.record->decoder;
    record_end_time = ctx->voice_effects.record->end_time_ms;

    int read_len = 0;
    ctx->is_zero = false;
    if (cur_position < record_start_time) {
        // zeros are added at the start or end of the recording.
        memset(buffer, 0, MAX_NB_SAMPLES * sizeof(*buffer));
        read_len = MAX_NB_SAMPLES;
        ctx->is_zero = true;
    } else if (cur_position >= record_start_time
        && cur_position < record_end_time) {
        read_len = IAudioDecoder_get_pcm_frame(record_decoder,
            buffer, MAX_NB_SAMPLES, false);
        if (read_len < 0) {
            if (read_len == PCM_FILE_EOF) {
                //update the decoder that point the next bgm
                AudioRecordSource_free(ctx->voice_effects.record);
                // zeros are added at the end of the recording.
                memset(buffer, 0, MAX_NB_SAMPLES * sizeof(*buffer));
                read_len = MAX_NB_SAMPLES;
                ctx->is_zero = true;
            } else {
                LogError("%s IAudioDecoder_get_pcm_frame failed\n", __func__);
                ret = read_len;
                return ret;
            }
        }
    } else {
        //update the decoder that point the next bgm
        AudioRecordSource_free(ctx->voice_effects.record);
        // zeros are added at the start or end of the recording.
        memset(buffer, 0, MAX_NB_SAMPLES * sizeof(*buffer));
        read_len = MAX_NB_SAMPLES;
        ctx->is_zero = true;
    }
    ctx->cur_size += (read_len * sizeof(*buffer));

    ret = read_len;
    return ret;
}

static int add_effects_and_write_fifo(XmEffectContext *ctx) {
    int ret = -1;
    if (!ctx || !ctx->buffer || !ctx->audio_fifo) return -1;

    if ((ret = read_pcm_frame(ctx, ctx->buffer)) < 0) {
        if (ret != PCM_FILE_EOF)
            LogError("%s read_pcm_frame failed.\n", __func__);
        return ret;
    }

    if (!ctx->is_zero
        && (ret = add_effects(ctx, ctx->buffer, ret)) < 0) {
        LogError("%s add_effects failed.\n", __func__);
        return ret;
    }

    ret = fifo_write(ctx->audio_fifo, ctx->buffer, ret);
    return ret;
}

void xm_audio_effect_freep(XmEffectContext **ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx || NULL == *ctx)
        return;
    XmEffectContext *self = *ctx;

    ae_abort_l(self);
    ae_free(self);
    pthread_mutex_destroy(&self->mutex);
    free(*ctx);
    *ctx = NULL;
}

void xm_audio_effect_stop(XmEffectContext *ctx) {
    LogInfo("%s\n", __func__);
    if (NULL == ctx)
        return;

    ae_abort_l(ctx);
}

int xm_audio_effect_get_progress(XmEffectContext *ctx) {
    if (NULL == ctx)
        return 0;

    int ret = 0;
    pthread_mutex_lock(&ctx->mutex);
    ret = ctx->progress;
    pthread_mutex_unlock(&ctx->mutex);

    return ret;
}

int xm_audio_effect_get_frame(XmEffectContext *ctx,
    short *buffer, int buffer_size_in_short) {
    int ret = -1;
    if (!ctx || !buffer || buffer_size_in_short <= 0)
        return ret;

    while (fifo_occupancy(ctx->audio_fifo) < (size_t) buffer_size_in_short) {
        ret = add_effects_and_write_fifo(ctx);
        if (ret < 0) {
            if (!ctx->flush) flush(ctx);
            if (0 < fifo_occupancy(ctx->audio_fifo)) {
                break;
            } else {
                goto end;
            }
        }
    }

    return fifo_read(ctx->audio_fifo, buffer, buffer_size_in_short);
end:
    return ret;
}

int xm_audio_effect_seekTo(XmEffectContext *ctx,
        int seek_time_ms) {
    LogInfo("%s seek_time_ms %d.\n", __func__, seek_time_ms);
    if (!ctx || !ctx->voice_effects.record
            || !ctx->voice_effects.recordQueue)
        return -1;

    ctx->seek_time_ms = seek_time_ms > 0 ? seek_time_ms : 0;
    ctx->cur_size = 0;

    flush(ctx);
    if (ctx->audio_fifo) fifo_clear(ctx->audio_fifo);
    ctx->flush = false;

    if (effects_parse(&(ctx->voice_effects), ctx->in_config_path) < 0) {
        LogError("%s effects_parse %s failed\n", __func__, ctx->in_config_path);
        return -1;
    }

    return record_source_seekTo(ctx->voice_effects.recordQueue,
        ctx->voice_effects.record, ctx->dst_sample_rate, ctx->dst_channels, ctx->seek_time_ms);
}

static int xm_audio_effect_add_effects_l(XmEffectContext *ctx,
    const char *out_pcm_path) {
    int ret = -1;
    if (!ctx || !out_pcm_path)
        return ret;

    FILE *writer = NULL;
    if ((ret = ae_open_file(&writer, out_pcm_path, true)) < 0) {
        LogError("%s open output file %s failed\n", __func__, out_pcm_path);
        goto fail;
    }

    WavContext wav_ctx;
    memset(&wav_ctx, 0, sizeof(WavContext));
    wav_ctx.is_wav = true;
    if (wav_write_header(writer, &wav_ctx) < 0) {
        LogError("%s 1 write wav header failed, out_pcm_path %s\n", __func__, out_pcm_path);
        ret = -1;
        goto fail;
    }

    ctx->seek_time_ms = 0;
    ctx->cur_size = 0;
    ctx->flush = false;
    uint32_t data_size_byte = 0;
    int file_duration = ctx->duration_ms;
    //if (file_duration > MAX_DURATION_MIX_IN_MS) file_duration = MAX_DURATION_MIX_IN_MS;
    while (!ctx->abort) {
        int cur_position = ctx->seek_time_ms +
            calculation_duration_ms(ctx->cur_size, ctx->dst_bits_per_sample/8,
            ctx->dst_channels, ctx->dst_sample_rate);
        int progress = ((float)cur_position / file_duration) * 100;
        pthread_mutex_lock(&ctx->mutex);
        ctx->progress = progress;
        pthread_mutex_unlock(&ctx->mutex);

        ret = xm_audio_effect_get_frame(ctx, ctx->buffer, MAX_NB_SAMPLES);
        if (ret <= 0) {
            LogInfo("xm_audio_effect_get_frame len <= 0.\n");
            break;
        }

        fwrite(ctx->buffer, sizeof(*(ctx->buffer)), ret, writer);
        data_size_byte += (ret * sizeof(*(ctx->buffer)));
    }

    wav_ctx.header.audio_format = 0x0001;
    wav_ctx.header.sample_rate = ctx->dst_sample_rate;
    wav_ctx.header.nb_channels = ctx->dst_channels;
    wav_ctx.header.bits_per_sample = ctx->dst_bits_per_sample;
    wav_ctx.header.block_align = ctx->dst_channels * (wav_ctx.header.bits_per_sample / 8);
    wav_ctx.header.byte_rate = wav_ctx.header.block_align * ctx->dst_sample_rate;
    wav_ctx.header.data_size = data_size_byte;
    // total file size minus the size of riff_id(4 byte) and riff_size(4 byte) itself
    wav_ctx.header.riff_size = wav_ctx.header.data_size +
        sizeof(wav_ctx.header) - 8;
    if (wav_write_header(writer, &wav_ctx) < 0) {
        LogError("%s 2 write wav header failed, out_pcm_path %s\n", __func__, out_pcm_path);
        ret = -1;
        goto fail;
    }

    if (PCM_FILE_EOF == ret) ret = 0;
fail:
    if (writer) {
        fclose(writer);
        writer = NULL;
    }
    return ret;
}

int xm_audio_effect_add_effects(XmEffectContext *ctx,
    const char *out_pcm_path) {
    LogInfo("%s out_pcm_path = %s.\n", __func__, out_pcm_path);
    int ret = -1;
    if (!ctx || !out_pcm_path) {
        return ret;
    }

    if (ae_chkst_l(ctx->ae_status) < 0) {
        return AEERROR_INVALID_STATE;
    }
    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_STARTED;
    pthread_mutex_unlock(&ctx->mutex);

    if ((ret = xm_audio_effect_add_effects_l(ctx, out_pcm_path)) < 0) {
        LogError("%s add_voice_effects failed\n", __func__);
        goto fail;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_COMPLETED;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
fail:
    ae_reset_l(ctx);
    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_ERROR;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
}

int xm_audio_effect_init(XmEffectContext *ctx,
    const char *in_config_path)
{
    int ret = -1;
    if (!ctx || !in_config_path) {
        return ret;
    }
    LogInfo("%s in_config_path = %s.\n", __func__, in_config_path);

    ae_reset_l(ctx);

    ctx->voice_effects.record = (AudioRecordSource *)calloc(1, sizeof(AudioRecordSource));
    if (NULL == ctx->voice_effects.record) {
        LogError("%s alloc AudioRecordSource failed.\n", __func__);
        ret = -1;
        goto fail;
    }

    ctx->voice_effects.recordQueue = AudioRecordSourceQueue_create();
    if (NULL == ctx->voice_effects.recordQueue) {
        LogError("%s alloc AudioRecordSourceQueue recordQueue failed.\n", __func__);
        ret = -1;
        goto fail;
    }

    ctx->in_config_path = av_strdup(in_config_path);
    if ((ret = effects_parse(&(ctx->voice_effects), in_config_path)) < 0) {
        LogError("%s effects_parse %s failed\n", __func__, in_config_path);
        goto fail;
    }

    if ((ret = update_record_source(ctx->voice_effects.recordQueue,
            ctx->voice_effects.record, DEFAULT_SAMPLE_RATE, DEFAULT_CHANNEL_NUMBER)) < 0) {
        LogError("%s get the first AudioRecordSource failed.\n", __func__);
        goto fail;
    }

    ctx->seek_time_ms = 0;
    ctx->cur_size = 0;
    ctx->dst_sample_rate = ctx->voice_effects.record->decoder->out_sample_rate;
    ctx->dst_channels = ctx->voice_effects.record->decoder->out_nb_channels;
    ctx->dst_bits_per_sample = ctx->voice_effects.record->decoder->out_bits_per_sample;
    ctx->duration_ms = ctx->voice_effects.duration_ms;

    if ((ret = voice_effects_init(ctx)) < 0) {
        LogError("%s voice_effects_init failed.\n", __func__);
        ret = -1;
        goto fail;
    }

    // Allocate buffer for audio fifo
    ctx->audio_fifo = fifo_create(sizeof(short));
    if (!ctx->audio_fifo) {
        LogError("%s Could not allocate audio FIFO\n", __func__);
        ret = AEERROR_NOMEM;
        goto fail;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_INITIALIZED;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
fail:
    ae_reset_l(ctx);
    pthread_mutex_lock(&ctx->mutex);
    ctx->ae_status = AE_STATE_ERROR;
    pthread_mutex_unlock(&ctx->mutex);
    return ret;
}

XmEffectContext *xm_audio_effect_create() {
    XmEffectContext *self = (XmEffectContext *)calloc(1, sizeof(XmEffectContext));
    if (NULL == self) {
        LogError("%s alloc XmEffectContext failed.\n", __func__);
        return NULL;
    }

    self->dst_sample_rate = DEFAULT_SAMPLE_RATE;
    self->dst_channels = DEFAULT_CHANNEL_NUMBER;
    memset(self->voice_effects.effects_info, 0, MAX_NB_EFFECTS * sizeof(char *));
    memset(self->voice_effects.effects, 0, MAX_NB_EFFECTS * sizeof(EffectContext *));
    pthread_mutex_init(&self->mutex, NULL);
    self->ae_status = AE_STATE_UNINIT;

    return self;
}
