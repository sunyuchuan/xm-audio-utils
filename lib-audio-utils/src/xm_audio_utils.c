#include "xm_audio_utils.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "error_def.h"
#include "tools/util.h"
#include "codec/audio_decoder.h"
#include "mixer_effects/fade_in_out.h"
#include "xm_audio_mixer.h"
#include "xm_audio_effects.h"

typedef struct Fade {
    int bgm_start_time_ms;
    int bgm_end_time_ms;
    int pcm_sample_rate;
    int pcm_nb_channels;
    float volume;
    FadeInOut fade_io;
} Fade;

struct XmAudioUtils {
    volatile int ref_count;
    AudioDecoder *bgm_decoder;
    AudioDecoder *music_decoder;
    XmEffectContext *effects_ctx;
    XmMixerContext *mixer;
    Fade *fade;
    pthread_mutex_t mutex;
};

static AudioDecoder** get_decoder(XmAudioUtils *self, int decoder_type) {
    if(NULL == self) {
        return NULL;
    }

    AudioDecoder **decoder = NULL;
    switch(decoder_type) {
        case BGM:
            decoder = &(self->bgm_decoder);
            break;
        case MUSIC:
            decoder = &(self->music_decoder);
            break;
        default:
            decoder = NULL;
            LogError("%s invalid decoder_type.\n", __func__);
            break;
    }
    return decoder;
}

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

    if (self->fade) {
        free(self->fade);
        self->fade = NULL;
    }
    if (self->bgm_decoder) {
        xm_audio_decoder_freep(&self->bgm_decoder);
    }
    if (self->music_decoder) {
        xm_audio_decoder_freep(&self->music_decoder);
    }
    if (self->mixer) {
        xm_audio_mixer_stop(self->mixer);
        xm_audio_mixer_freep(&(self->mixer));
    }
    if (self->effects_ctx) {
        xm_audio_effect_stop(self->effects_ctx);
        xm_audio_effect_freep(&(self->effects_ctx));
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

int xm_audio_utils_effect_get_frame(XmAudioUtils *self,
    short *buffer, int buffer_size_in_short) {
    if (!self || !buffer || buffer_size_in_short <= 0) {
        return -1;
    }

    return xm_audio_effect_get_frame(self->effects_ctx,
        buffer, buffer_size_in_short);
}

int xm_audio_utils_effect_seekTo(XmAudioUtils *self,
    int seek_time_ms) {
    LogInfo("%s seek_time_ms %d\n", __func__, seek_time_ms);
    if (!self) {
        return -1;
    }

    return xm_audio_effect_seekTo(self->effects_ctx, seek_time_ms);
}

int xm_audio_utils_effect_init(XmAudioUtils *self,
        const char *in_config_path) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if (!self || !in_config_path) {
        return -1;
    }

    xm_audio_effect_stop(self->effects_ctx);
    xm_audio_effect_freep(&(self->effects_ctx));

    self->effects_ctx = xm_audio_effect_create();
    if (!self->effects_ctx) {
        LogError("%s xm_audio_effect_create failed\n", __func__);
        ret = -1;
        goto end;
    }

    ret = xm_audio_effect_init(self->effects_ctx, in_config_path);
    if (ret < 0) {
        LogError("%s xm_audio_effect_init failed\n", __func__);
        goto end;
    }

end:
    return ret;
}

int xm_audio_utils_mixer_get_frame(XmAudioUtils *self,
    short *buffer, int buffer_size_in_short) {
    if (!self || !buffer || buffer_size_in_short <= 0) {
        return -1;
    }

    return xm_audio_mixer_get_frame(self->mixer,
        buffer, buffer_size_in_short);
}

int xm_audio_utils_mixer_seekTo(XmAudioUtils *self,
    int seek_time_ms) {
    LogInfo("%s seek_time_ms %d\n", __func__, seek_time_ms);
    if (!self) {
        return -1;
    }

    return xm_audio_mixer_seekTo(self->mixer, seek_time_ms);
}

int xm_audio_utils_mixer_init(XmAudioUtils *self,
        const char *in_config_path) {
    LogInfo("%s\n", __func__);
    int ret = -1;
    if (!self || !in_config_path) {
        return -1;
    }

    xm_audio_mixer_stop(self->mixer);
    xm_audio_mixer_freep(&(self->mixer));

    self->mixer = xm_audio_mixer_create();
    if (!self->mixer) {
        LogError("%s xm_audio_mixer_create failed\n", __func__);
        ret = -1;
        goto end;
    }

    ret = xm_audio_mixer_init(self->mixer, NULL, in_config_path);
    if (ret < 0) {
        LogError("%s xm_audio_mixer_init failed\n", __func__);
        goto end;
    }

end:
    return ret;
}

int xm_audio_utils_fade(XmAudioUtils *self, short *buffer,
        int buffer_size, int buffer_start_time) {
    if (!self || !buffer || !self->fade || buffer_size <= 0)
        return -1;
    Fade *fade = self->fade;
    int buffer_duration_ms = 1000 * ((float)buffer_size /
        fade->pcm_nb_channels / fade->pcm_sample_rate);

    check_fade_in_out(&fade->fade_io, buffer_start_time, buffer_duration_ms,
        fade->pcm_sample_rate, fade->bgm_start_time_ms, fade->bgm_end_time_ms);
    scale_with_ramp(&fade->fade_io, buffer,
        buffer_size / fade->pcm_nb_channels, fade->pcm_nb_channels, fade->volume);
    return 0;
}

int xm_audio_utils_fade_init(XmAudioUtils *self,
        int pcm_sample_rate, int pcm_nb_channels,
        int bgm_start_time_ms, int bgm_end_time_ms, int volume,
        int fade_in_time_ms, int fade_out_time_ms) {
    if (!self)
        return -1;
    LogInfo("%s\n", __func__);

    if (self->fade) {
        free(self->fade);
        self->fade = NULL;
    }
    self->fade = (Fade *)calloc(1, sizeof(Fade));
    if (NULL == self->fade) {
        LogError("%s calloc Fade failed.\n", __func__);
        return -1;
    }

    Fade *fade = self->fade;
    fade->bgm_start_time_ms = bgm_start_time_ms;
    fade->bgm_end_time_ms = bgm_end_time_ms;
    fade->volume = volume / (float)100;
    fade->pcm_sample_rate = pcm_sample_rate;
    fade->pcm_nb_channels = pcm_nb_channels;
    fade->fade_io.fade_in_time_ms = fade_in_time_ms;
    fade->fade_io.fade_out_time_ms = fade_out_time_ms;
    fade->fade_io.fade_in_nb_samples = fade_in_time_ms * pcm_sample_rate / 1000;
    fade->fade_io.fade_out_nb_samples = fade_out_time_ms * pcm_sample_rate / 1000;
    fade->fade_io.fade_in_samples_count = 0;
    fade->fade_io.fade_out_samples_count = 0;
    fade->fade_io.fade_out_start_index = 0;
    return 0;
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

