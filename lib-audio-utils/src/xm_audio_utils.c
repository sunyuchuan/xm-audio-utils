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
    AudioDecoder *bgm_decoder;
    AudioDecoder *music_decoder;
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

    if (self->bgm_decoder) {
        xm_audio_decoder_freep(&self->bgm_decoder);
    }
    if (self->music_decoder) {
        xm_audio_decoder_freep(&self->music_decoder);
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

static AudioDecoder** get_decoder(XmAudioUtils *self, int decoder_type) {
    if(NULL == self) {
        return NULL;
    }

    AudioDecoder **decoder = NULL;
    switch(decoder_type) {
        case DECODER_BGM:
            decoder = &(self->bgm_decoder);
            break;
        case DECODER_MUSIC:
            decoder = &(self->music_decoder);
            break;
        default:
            decoder = NULL;
            LogError("%s invalid decoder_type.\n", __func__);
            break;
    }
    return decoder;
}

int xm_audio_utils_get_decoded_frame(XmAudioUtils *self,
    short *buffer, int buffer_size_in_short, bool loop, int decoder_type) {
    int ret = -1;
    if(NULL == self || NULL == buffer
        || buffer_size_in_short <= 0) {
        return ret;
    }

    AudioDecoder **decoder = get_decoder(self, decoder_type);
    if (!decoder || !*decoder) {
        return ret;
    }

    ret = xm_audio_decoder_get_decoded_frame(*decoder, buffer, buffer_size_in_short, loop);
    if (ret == AVERROR_EOF) ret = 0;
    return ret;
}

void xm_audio_utils_decoder_seekTo(XmAudioUtils *self,
        int seek_time_ms, int decoder_type) {
    LogInfo("%s seek_time_ms %d\n", __func__, seek_time_ms);
    if(NULL == self) {
        return;
    }

    AudioDecoder **decoder = get_decoder(self, decoder_type);
    if (!decoder || !*decoder) {
        return;
    }

    xm_audio_decoder_seekTo(*decoder, seek_time_ms);
}

int xm_audio_utils_decoder_create(XmAudioUtils *self,
    const char *in_audio_path, int out_sample_rate,
    int out_channels, int decoder_type) {
    LogInfo("%s\n", __func__);
    if (NULL == self || NULL == in_audio_path) {
        return -1;
    }

    AudioDecoder **decoder = get_decoder(self, decoder_type);
    if (!decoder) {
        return -1;
    }
    xm_audio_decoder_freep(decoder);

    *decoder = xm_audio_decoder_create(in_audio_path,
        out_sample_rate, out_channels);
    if (*decoder == NULL) {
        LogError("xm_audio_decoder_create failed\n");
        return -1;
    }

    return 0;
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

