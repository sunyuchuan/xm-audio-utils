#ifndef _AUDIO_SOURCE_QUEUE_H_
#define _AUDIO_SOURCE_QUEUE_H_
#include "audio_source.h"
#include <pthread.h>

typedef struct AudioSourceList {
    AudioSource source;
    struct AudioSourceList *next;
} AudioSourceList;

typedef struct AudioSourceQueue
{
    AudioSourceList *mFirst;
    AudioSourceList *mLast;
    volatile int mNumbers;
    pthread_mutex_t mLock;
} AudioSourceQueue;

int source_queue_get_end_time_ms(AudioSourceQueue *queue);
void source_queue_bubble_sort(AudioSourceQueue *queue);
int source_queue_size(AudioSourceQueue *queue);
void source_queue_flush(AudioSourceQueue *queue);
void source_queue_free(AudioSourceQueue *queue);
void source_queue_freep(AudioSourceQueue **queue);
int source_queue_get(AudioSourceQueue *queue, AudioSource *source);
int source_queue_put(AudioSourceQueue *queue, AudioSource *source);
AudioSourceQueue *source_queue_create();

#endif
