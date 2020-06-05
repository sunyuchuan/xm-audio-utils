#ifndef XM_AUDIO_TRANSCODE_H_
#define XM_AUDIO_TRANSCODE_H_
#include "em_macro_def.h"

typedef struct XmAudioTranscoder XmAudioTranscoder;

#define TRANSCODER_STATE_UNINIT  0
#define TRANSCODER_STATE_INITIALIZED  1
#define TRANSCODER_STATE_STARTED  2
#define TRANSCODER_STATE_COMPLETED  3
#define TRANSCODER_STATE_ERROR  4

/**
 * @brief free XmAudioTranscoder
 *
 * @param self XmAudioTranscoder*
 */
EM_PORT_API(void) xm_audio_transcoder_freep(XmAudioTranscoder *self);

/**
 * @brief stop transcode
 *
 * @param self XmAudioTranscoder
 */
EM_PORT_API(void) xm_audio_transcoder_stop(XmAudioTranscoder *self);

/**
 * @brief get progress of transocder
 *
 * @param self XmAudioTranscoder
 */
EM_PORT_API(int) xm_audio_transcoder_get_progress(XmAudioTranscoder *self);

/**
 * @brief transcode audio file to m4a(aac) format
 *
 * @param XmAudioTranscoder transcoder
 * @param in_audio_path input audio file path
 * @param out_m4a_path output mp4 file path
 * @return Less than 0 means failure.
 */
EM_PORT_API(int) xm_audio_transcoder_start(XmAudioTranscoder *self,
    const char *in_audio_path, const char *out_m4a_path);

/**
 * @brief create XmAudioTranscoder
 *
 * @return XmAudioTranscoder*
 */
EM_PORT_API(XmAudioTranscoder *) xm_audio_transcoder_create();

#endif