#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <stddef.h>

typedef void (*tp_work_fn)(int client_fd);

typedef struct thread_pool_s
{
    pthread_t       *workers;
    size_t           worker_count;

    int             *queue;            /* ring buffer of client fds */
    size_t           queue_capacity;
    size_t           queue_head;       /* next slot to dequeue */
    size_t           queue_tail;       /* next slot to enqueue */
    size_t           queue_size;       /* number of fds currently queued */

    pthread_mutex_t  lock;
    pthread_cond_t   not_empty;

    tp_work_fn       work_fn;
} thread_pool_t;

/**
*   @brief  Initialise the pool: spawn `worker_count` threads, allocate a queue
*          of `queue_capacity` fds, and store the work function each worker runs
*          on every dequeued fd.
*
*   @return 0 on success, -1 on failure.
*/
int  thread_pool_init(thread_pool_t *pool,
                      size_t worker_count,
                      size_t queue_capacity,
                      tp_work_fn work_fn);

/**
*   @brief  Hand a client fd to the pool. If the queue is full, the fd is
*           closed and the call returns -1 (silent overload protection).
*
*   @return 0 if accepted, -1 if dropped.
*/
int  thread_pool_submit(thread_pool_t *pool, int client_fd);

#endif // THREAD_POOL_H
