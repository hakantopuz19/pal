/**
 * @file pal_mutex.h
 * @brief Public mutex interface for the PAL.
 */

#ifndef PAL_MUTEX_H
#define PAL_MUTEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pal_mutex pal_mutex_t;

/* Creates a mutex and stores the new object in the output parameter.
 * The returned mutex must eventually be released with pal_mutex_destroy(). */
int32_t pal_mutex_create(pal_mutex_t **mutex);

/* Destroys a mutex. The mutex must be unlocked and unused by all threads. */
int32_t pal_mutex_destroy(pal_mutex_t *mutex);

/* Locks the mutex, waiting until it becomes available. */
int32_t pal_mutex_lock(pal_mutex_t *mutex);

/* Unlocks a mutex previously locked by the calling thread. */
int32_t pal_mutex_unlock(pal_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif
