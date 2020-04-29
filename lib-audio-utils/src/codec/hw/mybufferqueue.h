#ifndef _MY_BUFFERQUEUE_H_
#define _MY_BUFFERQUEUE_H_

#ifndef BUFQUEUE_SIZE
#define BUFQUEUE_SIZE 32
#endif

typedef void (*func_free_node)(void **);

typedef struct BufQueue {
    void *queue[BUFQUEUE_SIZE];
    unsigned short head;
    unsigned short available; /**< number of available buffers */
    func_free_node free_node;
} BufQueue;

#define BUCKET(i) queue->queue[(queue->head + (i)) % BUFQUEUE_SIZE]

/**
 * Test if a buffer queue is full.
 */
static inline int bufqueue_is_full(BufQueue *queue)
{
    return queue->available == BUFQUEUE_SIZE;
}

/**
 * Test if a buffer queue is empty.
 */
static inline int bufqueue_is_empty(BufQueue *queue)
{
    return queue->available == 0;
}

/**
 * Add a buffer to the queue.
 *
 * If the queue is already full, then the current last buffer is dropped
 * (and unrefed) with a warning before adding the new buffer.
 */
static inline void bufqueue_add(BufQueue *queue, void *buf)
{
    if (bufqueue_is_full(queue)) {
        av_log(NULL, AV_LOG_WARNING, "Buffer queue overflow, dropping.\n");
        //av_frame_free(&BUCKET(--queue->available));
        if (queue->free_node) {
            queue->free_node(&BUCKET(--queue->available));
        }
    }
    BUCKET(queue->available++) = buf;
}

/**
 * Get a buffer from the queue without altering it.
 *
 * Buffer with index 0 is the first buffer in the queue.
 * Return NULL if the queue has not enough buffers.
 */
static inline void *bufqueue_peek(BufQueue *queue, unsigned index)
{
    return index < queue->available ? BUCKET(index) : NULL;
}

/**
 * Get the first buffer from the queue and remove it.
 *
 * Do not use on an empty queue.
 */
static inline void *bufqueue_get(BufQueue *queue)
{
    void *ret = queue->queue[queue->head];
    queue->available--;
    queue->queue[queue->head] = NULL;
    queue->head = (queue->head + 1) % BUFQUEUE_SIZE;
    return ret;
}

/**
 * Unref and remove all buffers from the queue.
 */
static inline void bufqueue_discard_all(BufQueue *queue)
{
    while (queue->available) {
        void *buf = bufqueue_get(queue);
        if (queue->free_node)
            queue->free_node(&buf);
    }
}

#undef BUCKET

#endif
