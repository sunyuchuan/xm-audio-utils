#ifndef XM_AUDIO_UTILS_H_
#define XM_AUDIO_UTILS_H_

#include <stdbool.h>

enum BgmType {
    NONE = -1,
    BGM,
    MUSIC
};

enum ActionType {
    AC_NONE = -1,
    ADD_EFFECTS,
    MIXER_MIX
};

typedef struct XmAudioUtils XmAudioUtils;

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
 * @brief Get frame data with voice effects
 *
 * @param self XmAudioUtils
 * @param buffer buffer for storing data
 * @param buffer_size_in_short buffer size
 * @return size of valid buffer obtained.
                  Less than or equal to 0 means failure or end.
 */
int xm_audio_utils_effect_get_frame(XmAudioUtils *self,
    short *buffer, int buffer_size_in_short);

/**
 * @brief file seekTo
 *
 * @param self XmAudioUtils
 * @param seek_time_ms seek target time in ms
 * @return Less than 0 means failure
 */
int xm_audio_utils_effect_seekTo(XmAudioUtils *self,
    int seek_time_ms);

/**
 * @brief init effect
 *
 * @param self XmAudioUtils
 * @param in_pcm_path Input pcm file path
 * @param pcm_sample_rate The sample rate of input pcm file
 * @param pcm_channels The channels of input pcm file
 * @param in_config_path Config file about bgm and music parameter
 * @return Less than 0 means failure
 */
int xm_audio_utils_effect_init(XmAudioUtils *self,
        const char *in_pcm_path, int pcm_sample_rate, int pcm_channels,
        const char *in_config_path);

/**
 * @brief get mixed frame
 *
 * @param self XmAudioUtils
 * @param buffer buffer for storing data
 * @param buffer_size_in_short buffer size
 * @return size of valid buffer obtained.
                  Less than or equal to 0 means failure or end.
 */
int xm_audio_utils_mixer_get_frame(XmAudioUtils *self,
    short *buffer, int buffer_size_in_short);

/**
 * @brief mixer seekTo
 *
 * @param self XmAudioUtils
 * @param seek_time_ms seek target time in ms
 * @return Less than 0 means failure
 */
int xm_audio_utils_mixer_seekTo(XmAudioUtils *self,
    int seek_time_ms);

/**
 * @brief init mixer
 *
 * @param self XmAudioUtils
 * @param in_pcm_path Input pcm file path
 * @param pcm_sample_rate The sample rate of input pcm file
 * @param pcm_channels The channels of input pcm file
 * @param in_config_path Config file about bgm and music parameter
 * @return Less than 0 means failure
 */
int xm_audio_utils_mixer_init(XmAudioUtils *self,
       const char *in_pcm_path, int pcm_sample_rate, int pcm_channels,
       int dst_sample_rate, int dst_channels, const char *in_config_path);

/**
 * @brief start fade bgm pcm data
 *
 * @param self XmAudioUtils
 * @param buffer input pcm data
 * @param buffer_size size of buffer
 * @param buffer_start_time the time at which the buffer starts at bgm
 * @return Less than 0 means error
 */
int xm_audio_utils_fade(XmAudioUtils *self, short *buffer,
    int buffer_size, int buffer_start_time);

/**
 * @brief init fade in out params
 *
 * @param self XmAudioUtils
 * @param pcm_sample_rate input pcm sample rate
 * @param pcm_nb_channels input pcm nb_channels
 * @param bgm_start_time_ms when the bgm starts playing
 * @param bgm_end_time_ms when the bgm ends playing
 * @param volume bgm volume value
 * @param fade_in_time_ms bgm fade in time
 * @param fade_out_time_ms bgm fade out time
 * @return Less than 0 means error
 */
int xm_audio_utils_fade_init(XmAudioUtils *self,
    int pcm_sample_rate, int pcm_nb_channels,
    int bgm_start_time_ms, int bgm_end_time_ms, int volume,
    int fade_in_time_ms, int fade_out_time_ms);

/**
 * @brief get pcm data from decoder
 *
 * @param self XmAudioUtils
 * @param buffer output buffer
 * @param buffer_size_in_short the size of output buffer
 * @param loop true : re-start decoding when the audio file ends
 * @param parser_type BGM or MUSIC
 * @return size of valid buffer obtained.
                  Less than or equal to 0 means failure or end
 */
int xm_audio_utils_get_parser_frame(XmAudioUtils *self,
    short *buffer, int buffer_size_in_short, bool loop, int parser_type);

/**
 * @brief decoder seekTo
 *
 * @param self XmAudioUtils
 * @param seek_time_ms seek target time in ms
 * @param parser_type BGM or MUSIC
 */
int xm_audio_utils_parser_seekTo(XmAudioUtils *self,
        int seek_time_ms, int parser_type);

/**
 * @brief create decoder that decode audio to pcm data
 *
 * @param self XmAudioUtils
 * @param in_pcm_path Input pcm file path
 * @param src_sample_rate input pcm sample_rate
 * @param src_channels input pcm nb_channels
 * @param dst_sample_rate Output pcm sample_rate
 * @param dst_channels Output pcm nb_channels
 * @param parser_type BGM or MUSIC
 * @return Less than 0 means failure
 */
int xm_audio_utils_parser_init(XmAudioUtils *self,
    const char *in_pcm_path, int src_sample_rate, int src_channels,
    int dst_sample_rate, int dst_channels, int parser_type);

/**
 * @brief create XmAudioUtils
 *
 * @return XmAudioUtils*
 */
XmAudioUtils *xm_audio_utils_create();

#endif
