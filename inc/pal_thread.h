/**
 * @file pal_thread.h
 * @brief Public thread lifecycle interface for the PAL.
 */

#ifndef PAL_THREAD_H
#define PAL_THREAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pal_thread pal_thread_t;
typedef void *(*pal_thread_fn_t)(void *argument);

/* Creates a joinable thread that starts by calling entry(argument).
 * The output is set to NULL if creation fails. The callback result is an
 * opaque pointer that can be retrieved with pal_thread_join(). */
int32_t pal_thread_create(
    pal_thread_fn_t entry,
    void *argument,
    pal_thread_t **thread);

/* Waits for a joinable thread to finish and optionally receives its result.
 * Pass NULL for result to discard it. A successfully joined thread handle is
 * released and must not be reused. */
int32_t pal_thread_join(pal_thread_t *thread, void **result);

/* Detaches a thread so its resources are reclaimed when it exits.
 * A successfully detached thread handle is released and must not be reused. */
int32_t pal_thread_detach(pal_thread_t *thread);

/* Terminates the calling thread immediately. When called from a joinable PAL
 * thread, result is made available to the thread that joins it. */
void pal_thread_exit(void *result);

#ifdef __cplusplus
}
#endif

#endif
