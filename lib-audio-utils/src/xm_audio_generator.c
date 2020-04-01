#include "xm_audio_generator.h"
#include "xm_audio_mixer.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "error_def.h"
#include "tools/util.h"

extern void RegisterFFmpeg();

struct XmAudioGenerator {
    volatile int status;
    XmMixerContext *mixer_ctx;
    pthread_mutex_t mutex;
};

static int chk_st_l(int state)
{
    if (state == GENERATOR_STATE_INITIALIZED ||
            state == GENERATOR_STATE_COMPLETED) {
        return 0;
    }

    LogError("%s state(%d) is invalid.\n", __func__, state);
    LogError("%s expecting status == GENERATOR_STATE_INITIALIZED or \
        state == GENERATOR_STATE_COMPLETED.\n", __func__);
    return -1;
}

static int mixer_mix(XmAudioGenerator *self, const char *in_config_path,
        const char *out_file_path, int encode_type) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if(!self || !in_config_path || !out_file_path) {
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

    ret = xm_audio_mixer_init(self->mixer_ctx, in_config_path);
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

void xm_audio_generator_free(XmAudioGenerator *self) {
    LogInfo("%s\n", __func__);
    if (NULL == self)
        return;

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

    xm_audio_mixer_stop(self->mixer_ctx);
}

int xm_audio_generator_get_progress(XmAudioGenerator *self) {
    if (NULL == self)
        return -1;

    return xm_audio_mixer_get_progress(self->mixer_ctx);
}

int xm_audio_generator_start(XmAudioGenerator *self,
        const char *in_config_path, const char *out_file_path) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if (!self || !in_config_path || !out_file_path) {
        return ret;
    }

    if (chk_st_l(self->status) < 0) {
        return AEERROR_INVALID_STATE;
    }
    pthread_mutex_lock(&self->mutex);
    self->status = GENERATOR_STATE_STARTED;
    pthread_mutex_unlock(&self->mutex);

    if ((ret = mixer_mix(self, in_config_path, out_file_path, 0)) < 0) {
        LogError("%s mixer_mix failed\n", __func__);
        goto end;
    }

end:
    pthread_mutex_lock(&self->mutex);
    self->status = GENERATOR_STATE_COMPLETED;
    pthread_mutex_unlock(&self->mutex);
    LogInfo("%s completed, ret = %d.\n", __func__, ret);
    return ret;
}

XmAudioGenerator *xm_audio_generator_create() {
    XmAudioGenerator *self = (XmAudioGenerator *)calloc(1, sizeof(XmAudioGenerator));
    if (NULL == self) {
        LogError("%s alloc XmAudioGenerator failed.\n", __func__);
        return NULL;
    }

    pthread_mutex_init(&self->mutex, NULL);
    self->status = GENERATOR_STATE_INITIALIZED;
    RegisterFFmpeg();
    return self;
}

