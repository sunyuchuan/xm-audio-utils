#include "xm_audio_generator.h"
#include "xm_audio_effects.h"
#include "xm_audio_mixer.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "error_def.h"
#include "tools/util.h"

struct XmAudioGenerator {
    volatile int ref_count;
    XmEffectContext *effects_ctx;
    XmMixerContext *mixer_ctx;
    pthread_mutex_t mutex;
};

static int mixer_mix(XmAudioGenerator *self, const char *in_pcm_path,
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

    ret = xm_audio_mixer_init(self->mixer_ctx, in_pcm_path,
        pcm_sample_rate, pcm_channels, in_config_path);
    if (ret < 0) {
        LogError("%s xm_audio_mixer_init failed\n", __func__);
        goto end;
    }

    ret = xm_audio_mixer_mix(self->mixer_ctx, out_file_path, encode_type);
    if (ret < 0) {
	LogError("%s xm_audio_mixer_mix failed\n", __func__);
	goto end;
    }

end:
    return ret;
}

static int add_voice_effects(XmAudioGenerator *self, const char *in_pcm_path,
        int pcm_sample_rate, int pcm_channels, const char *in_config_path, const char *out_pcm_path) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if(NULL == self || NULL == in_pcm_path
        || NULL == in_config_path || NULL == out_pcm_path) {
        return ret;
    }

    xm_audio_effect_stop(self->effects_ctx);
    xm_audio_effect_freep(&(self->effects_ctx));

    self->effects_ctx = xm_audio_effect_create();
    if (!self->effects_ctx) {
        LogError("%s xm_audio_effect_create failed.\n", __func__);
        ret = -1;
        goto end;
    }

    if ((ret = xm_audio_effect_init(self->effects_ctx, in_pcm_path,
        pcm_sample_rate, pcm_channels, in_config_path)) < 0) {
        LogError("%s xm_audio_effect_init failed.\n", __func__);
        goto end;
    }

    if ((ret = xm_audio_effect_add_effects(self->effects_ctx, out_pcm_path)) < 0) {
        LogError("%s xm_audio_effect_add_effects failed.\n", __func__);
        goto end;
    }

end:
    return ret;
}

void xmag_inc_ref(XmAudioGenerator *self)
{
    assert(self);
    __sync_fetch_and_add(&self->ref_count, 1);
}

void xmag_dec_ref(XmAudioGenerator *self)
{
    if (!self)
        return;

    int ref_count = __sync_sub_and_fetch(&self->ref_count, 1);
    if (ref_count == 0) {
        LogInfo("%s xmag_dec_ref(): ref=0\n", __func__);
        xm_audio_generator_freep(&self);
    }
}

void xmag_dec_ref_p(XmAudioGenerator **self)
{
    if (!self || !*self)
        return;

    xmag_dec_ref(*self);
    *self = NULL;
}

void xm_audio_generator_free(XmAudioGenerator *self) {
    LogInfo("%s\n", __func__);
    if (NULL == self)
        return;

    if (self->effects_ctx) {
        xm_audio_effect_stop(self->effects_ctx);
        xm_audio_effect_freep(&(self->effects_ctx));
    }
    if (self->mixer_ctx) {
        xm_audio_mixer_stop(self->mixer_ctx);
        xm_audio_mixer_freep(&(self->mixer_ctx));
    }
}

void xm_audio_generator_freep(XmAudioGenerator **ag) {
    LogInfo("%s\n", __func__);
    if (NULL == ag || NULL == *ag)
        return;
    XmAudioGenerator *self = *ag;

    xm_audio_generator_free(self);
    pthread_mutex_destroy(&self->mutex);
    free(*ag);
    *ag = NULL;
}

void xm_audio_generator_stop(XmAudioGenerator *self) {
    LogInfo("%s\n", __func__);
    if (NULL == self)
        return;

    xm_audio_effect_stop(self->effects_ctx);
    xm_audio_mixer_stop(self->mixer_ctx);
}

int xm_audio_generator_get_progress(XmAudioGenerator *self) {
    if (NULL == self)
        return -1;

    return (xm_audio_effect_get_progress(self->effects_ctx)
        + xm_audio_mixer_get_progress(self->mixer_ctx)) / 2;
}

int xm_audio_generator_start(XmAudioGenerator *self,
        const char *in_pcm_path, int pcm_sample_rate, int pcm_channels,
        const char *in_config_path, const char *out_file_path, int encode_type) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if (NULL == self || NULL == in_pcm_path
        || NULL == in_config_path || NULL == out_file_path) {
        return ret;
    }

    xm_audio_effect_stop(self->effects_ctx);
    xm_audio_effect_freep(&(self->effects_ctx));
    xm_audio_mixer_stop(self->mixer_ctx);
    xm_audio_mixer_freep(&(self->mixer_ctx));

    int len = 0;
    while(out_file_path[len++] != '\0');

    char *out_pcm_path = (char *)calloc(1, (len + 4) * sizeof(char));
    if (NULL == out_pcm_path) {
        LogError("%s calloc temp pcm file path failed.\n", __func__);
        return -1;
    }
    strncpy(out_pcm_path, out_file_path, len - 1);
    strncpy(out_pcm_path + len - 1, ".pcm", 5);

    if ((ret = add_voice_effects(self, in_pcm_path, pcm_sample_rate,
        pcm_channels, in_config_path, out_pcm_path)) < 0) {
        LogError("%s add_voice_effects failed\n", __func__);
        goto end;
    }

    if ((ret = mixer_mix(self, out_pcm_path, pcm_sample_rate,
        pcm_channels, in_config_path, out_file_path, encode_type)) < 0) {
        LogError("%s mixer_mix failed\n", __func__);
        goto end;
    }

end:
    if (out_pcm_path != NULL) {
        remove(out_pcm_path);
        free(out_pcm_path);
        out_pcm_path = NULL;
    }
    return ret;
}

XmAudioGenerator *xm_audio_generator_create() {
    XmAudioGenerator *self = (XmAudioGenerator *)calloc(1, sizeof(XmAudioGenerator));
    if (NULL == self) {
        LogError("%s alloc XmAudioGenerator failed.\n", __func__);
        return NULL;
    }

    pthread_mutex_init(&self->mutex, NULL);
    xmag_inc_ref(self);
    return self;
}

