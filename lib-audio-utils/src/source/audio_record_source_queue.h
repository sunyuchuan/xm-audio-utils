#ifndef _AUDIO_RECORD_SOURCE_QUEUE_H_
#define _AUDIO_RECORD_SOURCE_QUEUE_H_
#include "audio_record_source.h"
#include <pthread.h>

typedef struct AudioRecordSourceList {
    AudioRecordSource source;
    struct AudioRecordSourceList *next;
} AudioRecordSourceList;

typedef struct AudioRecordSourceQueue
{
    AudioRecordSourceList *mFirst;
    AudioRecordSourceList *mLast;
    volatile int mNumbers;
    pthread_mutex_t mLock;
} AudioRecordSourceQueue;

int AudioRecordSourceQueue_get_end_time_ms(AudioRecordSourceQueue *queue);
void AudioRecordSourceQueue_bubble_sort(AudioRecordSourceQueue *queue);
int AudioRecordSourceQueue_size(AudioRecordSourceQueue *queue);
void AudioRecordSourceQueue_flush(AudioRecordSourceQueue *queue);
void AudioRecordSourceQueue_free(AudioRecordSourceQueue *queue);
void AudioRecordSourceQueue_freep(AudioRecordSourceQueue **queue);
int AudioRecordSourceQueue_get(AudioRecordSourceQueue *queue, AudioRecordSource *source);
int AudioRecordSourceQueue_put(AudioRecordSourceQueue *queue, AudioRecordSource *source);
AudioRecordSourceQueue *AudioRecordSourceQueue_create();

#endif //_AUDIO_RECORD_SOURCE_QUEUE_H_