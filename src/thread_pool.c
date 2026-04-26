#include <stdlib.h>
#include <unistd.h>
#include "../include/thread_pool.h"

static void *worker_loop(void *arg)
{
    thread_pool_t *pool = (thread_pool_t *)arg;

    for (;;)
    {
        pthread_mutex_lock(&pool->lock);
        while (pool->queue_size == 0)
            pthread_cond_wait(&pool->not_empty, &pool->lock);

        int client_fd = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_capacity;
        pool->queue_size--;
        pthread_mutex_unlock(&pool->lock);

        pool->work_fn(client_fd);
    }
    return NULL;
}

int thread_pool_init(thread_pool_t *pool,
                     size_t worker_count,
                     size_t queue_capacity,
                     tp_work_fn work_fn)
{
    if (pool == NULL || worker_count == 0 || queue_capacity == 0 || work_fn == NULL)
        return -1;

    pool->workers = malloc(worker_count * sizeof(pthread_t));
    if (pool->workers == NULL) return -1;

    pool->queue = malloc(queue_capacity * sizeof(int));
    if (pool->queue == NULL) { free(pool->workers); return -1; }

    pool->worker_count   = worker_count;
    pool->queue_capacity = queue_capacity;
    pool->queue_head     = 0;
    pool->queue_tail     = 0;
    pool->queue_size     = 0;
    pool->work_fn        = work_fn;

    if (pthread_mutex_init(&pool->lock, NULL) != 0) goto fail_alloc;
    if (pthread_cond_init(&pool->not_empty, NULL) != 0)
    {
        pthread_mutex_destroy(&pool->lock);
        goto fail_alloc;
    }

    for (size_t i = 0; i < worker_count; i++)
    {
        if (pthread_create(&pool->workers[i], NULL, worker_loop, pool) != 0)
        {
            /* Best-effort: workers already created keep running; we only fail
               the init call. In practice this only happens at startup. */
            return -1;
        }
    }
    return 0;

fail_alloc:
    free(pool->queue);
    free(pool->workers);
    return -1;
}

int thread_pool_submit(thread_pool_t *pool, int client_fd)
{
    pthread_mutex_lock(&pool->lock);

    if (pool->queue_size == pool->queue_capacity)
    {
        pthread_mutex_unlock(&pool->lock);
        close(client_fd);
        return -1;
    }

    pool->queue[pool->queue_tail] = client_fd;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_capacity;
    pool->queue_size++;

    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);
    return 0;
}
