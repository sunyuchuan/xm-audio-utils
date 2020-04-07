#include "audio_source.h"
#include <stdlib.h>
#include <string.h>

void AudioSource_free(AudioSource *source) {
    if (source) {
        if (source->file_path || source->decoder) {
            if (source->file_path) {
                free(source->file_path);
                source->file_path = NULL;
            }
            IAudioDecoder_freep(&(source->decoder));
            memset(source, 0, sizeof(AudioSource));
        }
    }
}

void AudioSource_freep(AudioSource **source) {
    if (source && *source) {
        AudioSource_free(*source);
        free(*source);
        *source = NULL;
    }
}
