#ifndef XM_AUDIO_GENERATOR_H_
#define XM_AUDIO_GENERATOR_H_
#include "em_macro_def.h"

typedef struct XmAudioGenerator XmAudioGenerator;

enum GeneratorStatus {
    GS_ERROR = -1,
    GS_COMPLETED,
    GS_STOPPED,
};

#define GENERATOR_STATE_UNINIT  0
#define GENERATOR_STATE_INITIALIZED  1
#define GENERATOR_STATE_STARTED  2
#define GENERATOR_STATE_STOP  3
#define GENERATOR_STATE_COMPLETED  4
#define GENERATOR_STATE_ERROR  5

/**
 * @brief free XmAudioGenerator
 *
 * @param self XmAudioGenerator*
 */
EM_PORT_API(void) xm_audio_generator_free(XmAudioGenerator *self);

/**
 * @brief free XmAudioGenerator
 *
 * @param self XmAudioGenerator**
 */
EM_PORT_API(void) xm_audio_generator_freep(XmAudioGenerator *self);

/**
 * @brief stop generator
 *
 * @param self XmAudioGenerator
 */
EM_PORT_API(void) xm_audio_generator_stop(XmAudioGenerator *self);

/**
 * @brief get progress of generator
 *
 * @param self XmAudioGenerator
 */
EM_PORT_API(int) xm_audio_generator_get_progress(XmAudioGenerator *self);

/**
 * @brief set callback of progress
 *
 * @param self XmAudioGenerator
 * @param callback callback function name
 */
EM_PORT_API(int) xm_audio_generator_set_progress_callback(
    XmAudioGenerator *self, const char *callback);

/**
 * @brief startup add voice effects and mix voice\bgm\music
 *
 * @param self XmAudioGenerator
 * @param in_config_path Config file about audio mix parameter
 * @param out_file_path Output audio file path
 * @return GeneratorStatus
 */
EM_PORT_API(enum GeneratorStatus) xm_audio_generator_start(
    XmAudioGenerator *self, const char *in_config_path,
    const char *out_file_path);

/**
 * @brief create XmAudioGenerator
 *
 * @return XmAudioGenerator*
 */
EM_PORT_API(XmAudioGenerator *) xm_audio_generator_create();

#endif
