/**
 * @file pal_queue.c
 * @brief Platform-independent bounded FIFO queue implementation.
 */

#include <stdlib.h>

#include "pal_error.h"
#include "pal_mutex.h"
#include "pal_queue.h"
#include "pal_backend.h"

struct pal_queue
{
    void **items;
    size_t capacity;
    size_t count;
    size_t head;
    size_t tail;
    pal_mutex_t *mutex;
    void *not_empty;
    void *not_full;
};

/**
 * @brief      Waits for a queue condition using the remaining timeout.
 *
 * @details    Converts the operation's original timeout into the remaining
 *             duration before calling the platform condition wait. A timeout
 *             of zero is non-blocking, and UINT32_MAX means wait indefinitely.
 *
 * @param[in]  condition   Platform condition variable to wait on.
 * @param[in]  mutex       Mutex protecting the queue condition.
 * @param[in]  timeout_ms  Original timeout in milliseconds.
 * @param[in]  start_ms    Monotonic start time of the operation.
 *
 * @return     PAL_OK when the wait is signaled, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 The condition was signaled.
 * @retval     PAL_ERR_WOULD_BLOCK    The timeout is zero.
 * @retval     PAL_ERR_TIMEOUT        The requested duration elapsed.
 * @retval     Other PAL_ERR_*        Clock lookup or platform wait failed.
 *
 * @note       This helper is called while the queue mutex is held.
 * @warning    The caller must re-check the queue predicate after a successful
 *             wait because condition variables may wake spuriously.
 */
static int32_t pal_queue_wait(
    void *condition,
    pal_mutex_t *mutex,
    uint32_t timeout_ms,
    uint64_t start_ms)
{
    uint32_t remaining;
    uint64_t elapsed;
    uint64_t current_ms;
    int32_t status;

    if (timeout_ms == 0u)
    {
        return PAL_ERR_WOULD_BLOCK;
    }

    if (timeout_ms == UINT32_MAX)
    {
        return pal_sync_condition_backend_wait(condition, mutex, timeout_ms);
    }

    status = pal_sync_monotonic_time_ms(&current_ms);
    if (status != PAL_OK)
    {
        return status;
    }

    elapsed = current_ms - start_ms;
    if (elapsed >= (uint64_t)timeout_ms)
    {
        return PAL_ERR_TIMEOUT;
    }

    remaining = timeout_ms - (uint32_t)elapsed;
    return pal_sync_condition_backend_wait(condition, mutex, remaining);
}

/**
 * @brief      Creates a bounded FIFO queue.
 *
 * @details    Allocates queue storage and initializes its mutex and condition
 *             variables. Items are stored as pointer values; the queue does
 *             not copy or manage the objects they reference.
 *
 * @param[in]  capacity  Maximum number of items; must be greater than zero.
 * @param[out] queue     Receives the created queue.
 *
 * @return     PAL_OK when creation succeeds, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                   Queue creation succeeded.
 * @retval     PAL_ERR_NULL_POINTER     The output parameter is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT The capacity is zero.
 * @retval     PAL_ERR_OVERFLOW         The requested storage size overflows.
 * @retval     PAL_ERR_OUT_OF_MEMORY    An allocation failed.
 * @retval     Other PAL_ERR_*          A synchronization backend failed.
 *
 * @note       The output parameter is set to NULL before resources are
 *             allocated.
 * @warning    The caller owns the objects referenced by enqueued items.
 */
int32_t pal_queue_create(size_t capacity, pal_queue_t **queue)
{
    pal_queue_t *created;
    int32_t status;

    if (queue == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *queue = NULL;

    if (capacity == 0u)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    if (capacity > ((size_t)-1) / sizeof(*created->items))
    {
        return PAL_ERR_OVERFLOW;
    }

    created = (pal_queue_t *)calloc(1u, sizeof(*created));
    if (created == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }

    created->items = (void **)calloc(capacity, sizeof(*created->items));
    if (created->items == NULL)
    {
        free(created);
        return PAL_ERR_OUT_OF_MEMORY;
    }

    created->capacity = capacity;
    status = pal_mutex_create(&created->mutex);
    if (status != PAL_OK)
    {
        free(created->items);
        free(created);
        return status;
    }

    status = pal_sync_condition_backend_create(&created->not_empty);
    if (status != PAL_OK)
    {
        (void)pal_mutex_destroy(created->mutex);
        free(created->items);
        free(created);
        return status;
    }

    status = pal_sync_condition_backend_create(&created->not_full);
    if (status != PAL_OK)
    {
        (void)pal_sync_condition_backend_destroy(created->not_empty);
        (void)pal_mutex_destroy(created->mutex);
        free(created->items);
        free(created);
        return status;
    }

    *queue = created;
    return PAL_OK;
}

/**
 * @brief      Destroys a queue and releases its internal resources.
 *
 * @details    Destroys both condition variables and the queue mutex, then
 *             releases the item array and queue object. It does not destroy
 *             or free objects referenced by queued items.
 *
 * @param[in]  queue  Queue to destroy.
 *
 * @return     PAL_OK when destruction succeeds, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Queue destruction succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The queue parameter is NULL.
 * @retval     PAL_ERR_BUSY           A platform synchronization object is
 *                                    still in use.
 * @retval     Other PAL_ERR_*        A backend reported a destruction error.
 *
 * @note       All queued items are discarded; their referenced objects remain
 *             owned by the caller.
 * @warning    No operation or waiting thread may be using the queue.
 */
int32_t pal_queue_destroy(pal_queue_t *queue)
{
    int32_t status;

    if (queue == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_sync_condition_backend_destroy(queue->not_empty);
    if (status != PAL_OK)
    {
        return status;
    }
    status = pal_sync_condition_backend_destroy(queue->not_full);
    if (status != PAL_OK)
    {
        return status;
    }

    status = pal_mutex_destroy(queue->mutex);
    if (status != PAL_OK)
    {
        return status;
    }

    free(queue->items);
    free(queue);
    return PAL_OK;
}

/**
 * @brief      Adds an item to the tail of the queue.
 *
 * @details    Enqueues the pointer value in FIFO order. If the queue is full,
 *             the call waits according to @p timeout_ms. A timeout of zero
 *             returns immediately, while UINT32_MAX waits indefinitely.
 *
 * @param[in]  queue       Queue to update.
 * @param[in]  item        Pointer value to enqueue; NULL is permitted.
 * @param[in]  timeout_ms  Maximum time to wait in milliseconds.
 *
 * @return     PAL_OK when the item is enqueued, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 The item was enqueued.
 * @retval     PAL_ERR_NULL_POINTER   The queue parameter is NULL.
 * @retval     PAL_ERR_WOULD_BLOCK    The queue is full and timeout is zero.
 * @retval     PAL_ERR_TIMEOUT        The queue remained full until timeout.
 * @retval     Other PAL_ERR_*        A clock or synchronization operation
 *                                    failed.
 *
 * @note       The queue does not copy, free, or otherwise own the item.
 * @warning    The queue must remain alive for the duration of this call.
 */
int32_t pal_queue_push(pal_queue_t *queue, void *item, uint32_t timeout_ms)
{
    int32_t status;
    uint64_t start_ms;

    if (queue == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_sync_monotonic_time_ms(&start_ms);
    if (status != PAL_OK)
    {
        return status;
    }

    status = pal_mutex_lock(queue->mutex);
    if (status != PAL_OK)
    {
        return status;
    }

    while (queue->count == queue->capacity)
    {
        status = pal_queue_wait(queue->not_full, queue->mutex, timeout_ms, start_ms);
        if (status != PAL_OK)
        {
            (void)pal_mutex_unlock(queue->mutex);
            return status;
        }
    }

    queue->items[queue->tail] = item;
    queue->tail = (queue->tail + 1u) % queue->capacity;
    queue->count++;

    status = pal_sync_condition_backend_signal(queue->not_empty);
    if (status == PAL_OK)
    {
        status = pal_mutex_unlock(queue->mutex);
    }
    else
    {
        (void)pal_mutex_unlock(queue->mutex);
    }
    return status;
}

/**
 * @brief      Removes an item from the head of the queue.
 *
 * @details    Dequeues the oldest pointer value in FIFO order and writes it
 *             to @p item. If the queue is empty, the call waits according to
 *             @p timeout_ms. A timeout of zero returns immediately, while
 *             UINT32_MAX waits indefinitely.
 *
 * @param[in]  queue       Queue to read.
 * @param[out] item        Receives the dequeued pointer value.
 * @param[in]  timeout_ms  Maximum time to wait in milliseconds.
 *
 * @return     PAL_OK when an item is dequeued, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 An item was dequeued.
 * @retval     PAL_ERR_NULL_POINTER   The queue or output parameter is NULL.
 * @retval     PAL_ERR_WOULD_BLOCK    The queue is empty and timeout is zero.
 * @retval     PAL_ERR_TIMEOUT        The queue remained empty until timeout.
 * @retval     Other PAL_ERR_*        A clock or synchronization operation
 *                                    failed.
 *
 * @note       A queued NULL pointer is a valid item; check the status code to
 *             distinguish it from a failed operation.
 * @warning    The queue must remain alive for the duration of this call.
 */
int32_t pal_queue_pop(pal_queue_t *queue, void **item, uint32_t timeout_ms)
{
    int32_t status;
    uint64_t start_ms;

    if (queue == NULL || item == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_sync_monotonic_time_ms(&start_ms);
    if (status != PAL_OK)
    {
        return status;
    }

    status = pal_mutex_lock(queue->mutex);
    if (status != PAL_OK)
    {
        return status;
    }

    while (queue->count == 0u)
    {
        status = pal_queue_wait(queue->not_empty, queue->mutex, timeout_ms, start_ms);
        if (status != PAL_OK)
        {
            (void)pal_mutex_unlock(queue->mutex);
            return status;
        }
    }

    *item = queue->items[queue->head];
    queue->items[queue->head] = NULL;
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;

    status = pal_sync_condition_backend_signal(queue->not_full);
    if (status == PAL_OK)
    {
        status = pal_mutex_unlock(queue->mutex);
    }
    else
    {
        (void)pal_mutex_unlock(queue->mutex);
    }
    return status;
}
