#include "audio_source_queue.h"
#include <stdlib.h>
#include <string.h>
#include "log.h"

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

static bool audio_source_isValid(AudioSource *source)
{
    if(!source || !source->file_path)
        return false;

    return true;
}

void source_queue_bubble_sort(AudioSourceQueue *queue)
{
    if(!queue)
        return;
    AudioSourceList *sourceList, *prev, *next;

    pthread_mutex_lock(&queue->mLock);
    for (int i = queue->mNumbers - 1; i > 0; i--) {
        prev = NULL;sourceList = queue->mFirst;next = NULL;
        for (int j = 0; j < i; j++) {
            next = sourceList->next;
            if (sourceList->source.start_time_ms > next->source.start_time_ms) {
                sourceList->next = next->next;
                next->next = sourceList;
                if (prev) prev->next = next;
                prev = next;
            } else {
                prev = sourceList;
                sourceList = sourceList->next;
            }
        }
    }
    pthread_mutex_unlock(&queue->mLock);
}

int source_queue_size(AudioSourceQueue *queue)
{
    if(!queue)
        return -1;

    pthread_mutex_lock(&queue->mLock);
    int size = queue->mNumbers;
    pthread_mutex_unlock(&queue->mLock);
    return size;
}

void source_queue_flush(AudioSourceQueue *queue)
{
    if(!queue)
        return;

    AudioSourceList *sourceList, *next;

    pthread_mutex_lock(&queue->mLock);
    for(sourceList = queue->mFirst; sourceList != NULL; sourceList = next) {
        next = sourceList->next;
        audio_source_free(&(sourceList->source));
        free(sourceList);
    }
    queue->mLast = NULL;
    queue->mFirst = NULL;
    queue->mNumbers = 0;
    pthread_mutex_unlock(&queue->mLock);
}

void source_queue_free(AudioSourceQueue *queue)
{
    if(!queue)
        return;

    source_queue_flush(queue);
    pthread_mutex_destroy(&queue->mLock);
}

void source_queue_freep(AudioSourceQueue **queue)
{
    if(!queue || !*queue)
        return;

    source_queue_free(*queue);
    free(*queue);
    *queue = NULL;
}

int source_queue_get(AudioSourceQueue *queue, AudioSource *source)
{
    AudioSourceList *sourceList;
    int ret = -1;
    if(!queue || !source)
        return ret;

    pthread_mutex_lock(&queue->mLock);
    sourceList = queue->mFirst;
    if(sourceList) {
        queue->mFirst = sourceList->next;
        if (!queue->mFirst)
            queue->mLast = NULL;
        queue->mNumbers--;
        *source = sourceList->source;
        free(sourceList);
        ret = 1;
    } else {
        ret = -1;
    }
    pthread_mutex_unlock(&queue->mLock);
    return ret;
}

int source_queue_put(AudioSourceQueue *queue, AudioSource *source)
{
    AudioSourceList *sourceList;

    if (!queue || !audio_source_isValid(source))
        return -1;

    sourceList = (AudioSourceList *)calloc(1, sizeof(AudioSourceList));
    if (!sourceList)
        return -1;
    sourceList->source = *source;
    sourceList->next = NULL;

    pthread_mutex_lock(&queue->mLock);
    if(!queue->mLast) {
        queue->mFirst = sourceList;
    } else {
        queue->mLast->next = sourceList;
    }
    queue->mLast = sourceList;
    queue->mNumbers++;
    pthread_mutex_unlock(&queue->mLock);
    return 0;
}

AudioSourceQueue *source_queue_create()
{
    AudioSourceQueue *queue = (AudioSourceQueue *)calloc(1, sizeof(AudioSourceQueue));
    if (!queue)
        return NULL;

    pthread_mutex_init(&queue->mLock, NULL);
    return queue;
}
