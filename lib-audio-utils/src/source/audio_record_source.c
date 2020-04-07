#include "audio_record_source.h"
#include <stdlib.h>
#include <string.h>

void AudioRecordSource_free(AudioRecordSource *record) {
    if (record) {
        if (record->file_path || record->decoder) {
            if (record->file_path) {
                free(record->file_path);
                record->file_path = NULL;
            }
            IAudioDecoder_freep(&(record->decoder));
            memset(record, 0, sizeof(AudioRecordSource));
        }
    }
}

void AudioRecordSource_freep(AudioRecordSource **record) {
    if (record && *record) {
        AudioRecordSource_free(*record);
        free(*record);
        *record = NULL;
    }
}

