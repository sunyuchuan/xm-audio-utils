#if defined(__ANDROID__) || defined (__linux__)

#ifndef XM_AUDIO_UTILS_H_
#define XM_AUDIO_UTILS_H_

#include <stdbool.h>

enum DecoderType {
    DECODER_NONE = -1,
    DECODER_BGM,
    DECODER_MUSIC
};

typedef struct XmAudioUtils XmAudioUtils;

/**
 * @brief Reference count subtract 1
 *
 * @param self XmAudioUtils
 */
void xmau_dec_ref(XmAudioUtils *self);

/**
 * @brief Reference count subtract 1
 *
 * @param self XmAudioUtils
 */
void xmau_dec_ref_p(XmAudioUtils **self);

/**
 * @brief Reference count plus 1
 *
 * @param self XmAudioUtils
 */
void xmau_inc_ref(XmAudioUtils *self);

/**
 * @brief free XmAudioUtils
 *
 * @param self XmAudioUtils*
 */
void xm_audio_utils_free(XmAudioUtils *self);

/**
 * @brief free XmAudioUtils
 *
 * @param self XmAudioUtils**
 */
void xm_audio_utils_freep(XmAudioUtils **self);

/**
 * @brief stop mixer_mix
 *
 * @param self XmAudioUtils
 */
void stop_mixer_mix(XmAudioUtils *self);

/**
 * @brief get progress of mixer_mix
 *
 * @param self XmAudioUtils
 */
int get_progress_mix(XmAudioUtils *self);

/**
 * @brief mix voice\bgm\music
 *
 * @param self XmAudioUtils
 * @param in_pcm_path Input pcm file path of voice
 * @param pcm_sample_rate sample_rate of in_pcm_file
 * @param pcm_channels channels of in_pcm_file
 * @param in_config_path Config file about audio mix parameter
 * @param out_file_path Output audio file path
 * @param encode_type 0:ffmpeg encoder,1:mediacodec encoder
 * @return Less than 0 means failure
 */
int mixer_mix(XmAudioUtils *self, const char *in_pcm_path,
        int pcm_sample_rate, int pcm_channels, const char *in_config_path,
        const char *out_file_path, int encode_type);

/**
 * @brief stop add voice effects
 *
 * @param self XmAudioUtils
 */
void stop_add_effects(XmAudioUtils *self);

/**
 * @brief get progress of add_voice_effects
 *
 * @param self XmAudioUtils
 */
int get_progress_effects(XmAudioUtils *self);

/**
 * @brief Add voice effects
 *
 * @param self XmAudioUtils
 * @param in_pcm_path Input pcm file path of voice
 * @param sample_rate sample_rate of in_pcm_file
 * @param channels channels of in_pcm_file
 * @param in_config_path Config file about audio effect parameter
 * @param out_pcm_path Output pcm file path
 * @return Less than 0 means failure
 */
int add_voice_effects(XmAudioUtils *self, const char *in_pcm_path,
    int sample_rate, int channels, const char *in_config_path, const char *out_pcm_path);

/**
 * @brief get pcm data from decoder
 *
 * @param self XmAudioUtils
 * @param buffer output buffer
 * @param buffer_size_in_short the size of output buffer
 * @param loop true : re-start decoding when the audio file ends
 * @param decoder_type DECODER_BGM or DECODER_MUSIC
 * @return size of valid buffer obtained.
                  Less than or equal to 0 means failure or end
 */
int xm_audio_utils_get_decoded_frame(XmAudioUtils *self,
    short *buffer, int buffer_size_in_short, bool loop, int decoder_type);

/**
 * @brief decoder seekTo
 *
 * @param self XmAudioUtils
 * @param seek_time_ms seek target time in ms
 * @param decoder_type DECODER_BGM or DECODER_MUSIC
 */
void xm_audio_utils_decoder_seekTo(XmAudioUtils *self,
    int seek_time_ms, int decoder_type);

/**
 * @brief create decoder that decode audio to pcm data
 *
 * @param self XmAudioUtils
 * @param in_audio_path Input audio file path
 * @param out_sample_rate Output audio sample_rate
 * @param out_channels Output audio nb_channels
 * @param decoder_type DECODER_BGM or DECODER_MUSIC
 * @return Less than 0 means failure
 */
int xm_audio_utils_decoder_create(XmAudioUtils *self,
    const char *in_audio_path, int out_sample_rate, int out_channels, int decoder_type);

/**
 * @brief create XmAudioUtils
 *
 * @return XmAudioUtils*
 */
XmAudioUtils *xm_audio_utils_create();

#endif
#endif
