#include "audio_source.h"
#include <stdlib.h>
#include <string.h>

void audio_source_free(AudioSource *source) {
    if (source) {
        if (source->file_path) {
            free(source->file_path);
            source->file_path = NULL;
        }
        pcm_parser_freep(&(source->parser));
        memset(source, 0, sizeof(AudioSource));
    }
}

void audio_source_freep(AudioSource **source) {
    if (source && *source) {
        audio_source_free(*source);
        free(*source);
        *source = NULL;
    }
}

void audio_record_source_free(AudioRecordSource *record) {
    if (record) {
        if (record->file_path) {
            free(record->file_path);
            record->file_path = NULL;
        }
        memset(record, 0, sizeof(AudioRecordSource));
    }
}

void audio_record_source_freep(AudioRecordSource **record) {
    if (record && *record) {
        audio_record_source_free(*record);
        free(*record);
        *record = NULL;
    }
}

