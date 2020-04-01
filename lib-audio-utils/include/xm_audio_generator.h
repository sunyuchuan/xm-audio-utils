#ifndef XM_AUDIO_GENERATOR_H_
#define XM_AUDIO_GENERATOR_H_

typedef struct XmAudioGenerator XmAudioGenerator;

#define GENERATOR_STATE_UNINIT  0
#define GENERATOR_STATE_INITIALIZED  1
#define GENERATOR_STATE_STARTED  2
#define GENERATOR_STATE_COMPLETED  3
#define GENERATOR_STATE_ERROR  4

/**
 * @brief free XmAudioGenerator
 *
 * @param self XmAudioGenerator*
 */
void xm_audio_generator_free(XmAudioGenerator *self);

/**
 * @brief free XmAudioGenerator
 *
 * @param self XmAudioGenerator**
 */
void xm_audio_generator_freep(XmAudioGenerator **self);

/**
 * @brief stop generator
 *
 * @param self XmAudioGenerator
 */
void xm_audio_generator_stop(XmAudioGenerator *self);

/**
 * @brief get progress of generator
 *
 * @param self XmAudioGenerator
 */
int xm_audio_generator_get_progress(XmAudioGenerator *self);

/**
 * @brief startup add voice effects and mix voice\bgm\music
 *
 * @param self XmAudioGenerator
 * @param in_config_path Config file about audio mix parameter
 * @param out_file_path Output audio file path
 * @return Less than 0 means failure
 */
int xm_audio_generator_start(XmAudioGenerator *self,
    const char *in_config_path, const char *out_file_path);

/**
 * @brief create XmAudioGenerator
 *
 * @return XmAudioGenerator*
 */
XmAudioGenerator *xm_audio_generator_create();

#endif
