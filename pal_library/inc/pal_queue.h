/**
 * @file pal_queue.h
 * @brief Public bounded FIFO queue interface for the PAL.
 */

#ifndef PAL_QUEUE_H
#define PAL_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pal_queue pal_queue_t;

/* Creates a bounded FIFO queue that stores pointer values.
 * The queue does not copy or own the objects referenced by those pointers.
 * Capacity must be greater than zero. */
int32_t pal_queue_create(size_t capacity, pal_queue_t **queue);

/* Destroys a queue and discards any remaining pointer values.
 * No operation or waiting thread may still be using the queue. */
int32_t pal_queue_destroy(pal_queue_t *queue);

/* Adds an item at the queue tail.
 * timeout_ms is PAL_NO_WAIT, PAL_WAIT_FOREVER, or a finite timeout in ms.
 * A NULL item is permitted; PAL_ERR_WOULD_BLOCK or PAL_ERR_TIMEOUT indicates
 * that the queue remained full for the requested duration. */
int32_t pal_queue_push(pal_queue_t *queue, void *item, uint32_t timeout_ms);

/* Removes the oldest item from the queue and stores it in item.
 * timeout_ms is PAL_NO_WAIT, PAL_WAIT_FOREVER, or a finite timeout in ms.
 * A NULL item value is permitted; PAL_ERR_WOULD_BLOCK or PAL_ERR_TIMEOUT
 * indicates that the queue remained empty for the requested duration. */
int32_t pal_queue_pop(pal_queue_t *queue, void **item, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
