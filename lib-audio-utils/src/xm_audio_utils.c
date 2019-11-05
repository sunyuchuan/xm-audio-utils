#include "xm_audio_utils.h"
#include "xm_audio_effects.h"
#include "xm_audio_mixer.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "error_def.h"
#include "tools/util.h"
#include "codec/audio_decoder.h"

struct XmAudioUtils {
    volatile int ref_count;
    AudioDecoder *decoder;
    XmEffectContext *effects_ctx;
    XmMixerContext *mixer_ctx;
    pthread_mutex_t mutex;
};

void xmau_inc_ref(XmAudioUtils *self)
{
    assert(self);
    __sync_fetch_and_add(&self->ref_count, 1);
}

void xmau_dec_ref(XmAudioUtils *self)
{
    if (!self)
        return;

    int ref_count = __sync_sub_and_fetch(&self->ref_count, 1);
    if (ref_count == 0) {
        LogInfo("%s xmau_dec_ref(): ref=0\n", __func__);
        xm_audio_utils_freep(&self);
    }
}

void xmau_dec_ref_p(XmAudioUtils **self)
{
    if (!self || !*self)
        return;

    xmau_dec_ref(*self);
    *self = NULL;
}

void xm_audio_utils_free(XmAudioUtils *self) {
    LogInfo("%s\n", __func__);
    if (NULL == self)
        return;

    if (self->decoder) {
        xm_audio_decoder_freep(&self->decoder);
    }
    if (self->effects_ctx) {
        xm_audio_effect_stop(self->effects_ctx);
        xm_audio_effect_freep(&(self->effects_ctx));
    }
    if (self->mixer_ctx) {
        xm_audio_mixer_stop(self->mixer_ctx);
        xm_audio_mixer_freep(&(self->mixer_ctx));
    }
}

void xm_audio_utils_freep(XmAudioUtils **au) {
    LogInfo("%s\n", __func__);
    if (NULL == au || NULL == *au)
        return;
    XmAudioUtils *self = *au;

    xm_audio_utils_free(self);
    pthread_mutex_destroy(&self->mutex);
    free(*au);
    *au = NULL;
}

void stop_mixer_mix(XmAudioUtils *self) {
    LogInfo("%s\n", __func__);
    if (NULL == self)
        return;

    xm_audio_mixer_stop(self->mixer_ctx);
}

int get_progress_mix(XmAudioUtils *self) {
    if (NULL == self)
        return 0;

    return xm_audio_mixer_get_progress(self->mixer_ctx);
}

int mixer_mix(XmAudioUtils *self, const char *in_pcm_path,
        int pcm_sample_rate, int pcm_channels, const char *in_config_path,
        const char *out_file_path, int encode_type) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if(NULL == self || NULL == in_pcm_path
        || NULL == in_config_path || NULL == out_file_path) {
        return ret;
    }

    xm_audio_mixer_stop(self->mixer_ctx);
    xm_audio_mixer_freep(&(self->mixer_ctx));

    self->mixer_ctx = xm_audio_mixer_create();
    if (!self->mixer_ctx) {
        LogError("%s xm_audio_mixer_create failed\n", __func__);
        ret = -1;
        goto end;
    }

    ret = xm_audio_mixer_mix(self->mixer_ctx, in_pcm_path,
	pcm_sample_rate, pcm_channels,
	encode_type, in_config_path, out_file_path);
    if (ret < 0) {
	LogError("%s xm_audio_mixer_mix failed\n", __func__);
	goto end;
    }

end:
    return ret;
}

void stop_add_effects(XmAudioUtils *self) {
    LogInfo("%s\n", __func__);
    if (NULL == self)
        return;

    xm_audio_effect_stop(self->effects_ctx);
}

int get_progress_effects(XmAudioUtils *self) {
    if (NULL == self)
        return 0;

    return xm_audio_effect_get_progress(self->effects_ctx);
}

int add_voice_effects(XmAudioUtils *self, const char *in_pcm_path,
        int pcm_sample_rate, int pcm_channels, const char *in_config_path, const char *out_pcm_path) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if(NULL == self || NULL == in_pcm_path
        || NULL == in_config_path || NULL == out_pcm_path) {
        return ret;
    }

    xm_audio_effect_stop(self->effects_ctx);
    xm_audio_effect_freep(&(self->effects_ctx));

    self->effects_ctx = xm_audio_effect_create(pcm_sample_rate, pcm_channels);
    if (!self->effects_ctx) {
        LogError("%s xm_audio_effect_create failed\n", __func__);
        ret = -1;
        goto end;
    }

    if((ret = xm_audio_effect_add(self->effects_ctx, in_pcm_path,
            in_config_path, out_pcm_path)) < 0) {
        LogError("%s xm_audio_effect_add failed\n", __func__);
        goto end;
    }

end:
    return ret;
}

int xm_audio_utils_decode(XmAudioUtils *self, const char *in_audio_path,
    const char *out_pcm_file, int out_sample_rate, int out_channels) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if(NULL == self || NULL == in_audio_path
        || NULL == out_pcm_file) {
        return ret;
    }
    xm_audio_decoder_freep(&self->decoder);

    int buffer_size_in_short = 1024;
    short *buffer = NULL;

    FILE *pcm_writer = NULL;
    if ((ret = ae_open_file(&pcm_writer, out_pcm_file, true)) < 0) {
        LogError("%s open output file %s failed\n", __func__, out_pcm_file);
        goto end;
    }

    buffer = (short *)calloc(sizeof(short), buffer_size_in_short);
    if (!buffer) {
        LogError("%s calloc buffer failed\n", __func__);
        ret = AEERROR_NOMEM;
        goto end;
    }

    self->decoder = xm_audio_decoder_create(in_audio_path,
        out_sample_rate, out_channels);
    if (self->decoder == NULL) {
        LogError("xm_audio_decoder_create failed\n");
        ret = -1;
        goto end;
    }

    while (true) {
        ret = xm_audio_decoder_get_decoded_frame(self->decoder, buffer, buffer_size_in_short, false);
        if (ret <= 0) break;
        fwrite(buffer, sizeof(short), ret, pcm_writer);
    }
    if (ret == AVERROR_EOF) ret = 0;

end:
    if (buffer) {
        free(buffer);
        buffer = NULL;
    }
    if (pcm_writer) {
        fclose(pcm_writer);
        pcm_writer = NULL;
    }
}

XmAudioUtils *xm_audio_utils_create() {
    XmAudioUtils *self = (XmAudioUtils *)calloc(1, sizeof(XmAudioUtils));
    if (NULL == self) {
        LogError("%s alloc XmAudioUtils failed.\n", __func__);
        return NULL;
    }

    pthread_mutex_init(&self->mutex, NULL);
    xmau_inc_ref(self);
    return self;
}

