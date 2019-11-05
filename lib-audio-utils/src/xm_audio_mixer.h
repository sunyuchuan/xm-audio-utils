#ifndef XM_AUDIO_MIXER_H_
#define XM_AUDIO_MIXER_H_

typedef struct XmMixerContext_T XmMixerContext;

#define MIX_STATE_UNINIT  0
#define MIX_STATE_INITIALIZED  1
#define MIX_STATE_STARTED  2
#define MIX_STATE_COMPLETED  3
#define MIX_STATE_ERROR  4

/**
 * @brief free XmMixerContext
 *
 * @param ctx
 */
void xm_audio_mixer_freep(XmMixerContext **ctx);

/**
 * @brief stop mix
 *
 * @param ctx
 */
void xm_audio_mixer_stop(XmMixerContext *ctx);

/**
 * @brief get progress
 *
 * @param ctx
 */
int xm_audio_mixer_get_progress(XmMixerContext *ctx);

/**
 * @brief mix bgm and music
 *
 * @param ctx XmMixerContext
 * @param in_pcm_path Input pcm file path
 * @param pcm_sample_rate The sample rate of input pcm file
 * @param pcm_channels The channels of input pcm file
 * @param encoder_type Support ffmpeg and MediaCodec
 * @param in_config_path Config file about bgm and music parameter
 * @param out_file_path output mp4 file path
 * @return Less than 0 means failure
 */
int xm_audio_mixer_mix(XmMixerContext *ctx, const char *in_pcm_path,
        int pcm_sample_rate, int pcm_channels, int encoder_type,
        const char *in_config_path, const char *out_file_path);

/**
 * @brief create XmMixerContext
 *
 * @param sample_rate The sample rate of output audio
 * @param channels The channels of output audio
 * @return XmMixerContext*
 */
XmMixerContext *xm_audio_mixer_create();

#endif
