/**
 * @file pal_mutex.c
 * @brief Platform-independent mutex API implementation.
 */

#include <stdlib.h>

#include "pal_error.h"
#include "pal_mutex.h"
#include "pal_backend.h"

struct pal_mutex
{
    void *backend_state;
};

/**
 * @brief      Creates a mutex and initializes its platform-specific state.
 *
 * @details    Allocates the public mutex object, initializes the selected
 *             platform backend, and stores the result in the output
 *             parameter. The output is set to NULL before initialization.
 *
 * @param[out] mutex  Receives the created mutex.
 *
 * @return     PAL_OK when the mutex is created, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 Mutex creation succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The output parameter is NULL.
 * @retval     PAL_ERR_OUT_OF_MEMORY  Memory allocation failed.
 * @retval     Other PAL_ERR_*        The platform backend could not initialize.
 *
 * @note       The returned mutex must eventually be released with
 *             pal_mutex_destroy().
 * @warning    The output pointer must not refer to an existing live mutex
 *             pointer unless the caller has preserved that pointer elsewhere.
 */
int32_t pal_mutex_create(pal_mutex_t **mutex)
{
    pal_mutex_t *created;
    int32_t status;

    if (mutex == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    *mutex = NULL;
    created = (pal_mutex_t *)calloc(1u, sizeof(*created));
    if (created == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }

    status = pal_sync_mutex_backend_create(&created->backend_state);
    if (status != PAL_OK)
    {
        free(created);
        return status;
    }

    *mutex = created;
    return PAL_OK;
}

/**
 * @brief      Destroys a mutex and releases its platform-specific state.
 *
 * @details    Destroys the native synchronization object and frees the PAL
 *             wrapper. If the backend reports that the mutex is still busy,
 *             the wrapper remains allocated so the caller can resolve the
 *             outstanding use and retry.
 *
 * @param[in]  mutex  Mutex to destroy.
 *
 * @return     PAL_OK when destruction succeeds, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 Mutex destruction succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The mutex parameter is NULL.
 * @retval     PAL_ERR_BUSY           The mutex is locked or otherwise in use.
 * @retval     Other PAL_ERR_*        The platform backend reported an error.
 *
 * @note       Destroying a mutex does not synchronize with concurrent users.
 * @warning    The mutex must be unlocked and no thread may be using it.
 */
int32_t pal_mutex_destroy(pal_mutex_t *mutex)
{
    int32_t status;

    if (mutex == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_sync_mutex_backend_destroy(mutex->backend_state);
    if (status != PAL_OK)
    {
        return status;
    }

    free(mutex);
    return PAL_OK;
}

/**
 * @brief      Locks a mutex, waiting until it becomes available.
 *
 * @details    Delegates acquisition to the platform backend. The mutex is
 *             non-recursive; attempting to lock it again from its owning
 *             thread is not supported.
 *
 * @param[in]  mutex  Mutex to lock.
 *
 * @return     PAL_OK when the mutex is acquired, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Mutex acquisition succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The mutex parameter is NULL.
 * @retval     Other PAL_ERR_*        The platform backend reported an error.
 *
 * @note       A successful call must be paired with pal_mutex_unlock() by the
 *             owning thread.
 * @warning    Do not destroy the mutex while this call or any other operation
 *             is using it.
 */
int32_t pal_mutex_lock(pal_mutex_t *mutex)
{
    if (mutex == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    return pal_sync_mutex_backend_lock(mutex->backend_state);
}

/**
 * @brief      Unlocks a mutex held by the calling thread.
 *
 * @details    Delegates release to the platform backend. Unlocking a mutex
 *             that is not owned by the calling thread is invalid.
 *
 * @param[in]  mutex  Mutex to unlock.
 *
 * @return     PAL_OK when the mutex is released, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                   Mutex release succeeded.
 * @retval     PAL_ERR_NULL_POINTER     The mutex parameter is NULL.
 * @retval     PAL_ERR_INVALID_STATE    The calling thread does not own it or
 *                                      the backend rejected the operation.
 * @retval     Other PAL_ERR_*          The platform backend reported an error.
 *
 * @note       Only the thread that successfully locked the mutex may unlock it.
 * @warning    Unlocking an unlocked mutex has platform-dependent consequences
 *             and must be avoided.
 */
int32_t pal_mutex_unlock(pal_mutex_t *mutex)
{
    if (mutex == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    return pal_sync_mutex_backend_unlock(mutex->backend_state);
}

/**
 * @brief      Acquires the process-wide PAL mutex.
 *
 * @details    Delegates to the statically initialized mutex in the selected
 *             synchronization backend.
 *
 * @return     PAL_OK when acquired or a negative PAL error code.
 *
 * @note       The global mutex is non-recursive and requires no lifecycle calls.
 * @warning    Pair a successful call with pal_mutex_global_unlock().
 */
int32_t pal_mutex_global_lock(void)
{
    return pal_sync_global_mutex_backend_lock();
}

/**
 * @brief      Releases the process-wide PAL mutex.
 *
 * @details    Delegates release to the selected synchronization backend.
 *
 * @return     PAL_OK when released or a negative PAL error code.
 *
 * @note       Only the owning thread may release the mutex.
 * @warning    Unlocking the global mutex without owning it is invalid.
 */
int32_t pal_mutex_global_unlock(void)
{
    return pal_sync_global_mutex_backend_unlock();
}

/**
 * @brief      Returns the opaque platform state associated with a mutex.
 *
 * @details    Provides internal platform synchronization code with access to
 *             the native object without exposing its representation through
 *             the public API.
 *
 * @param[in]  mutex  Mutex whose platform state is requested.
 *
 * @return     The native backend state, or NULL if the mutex is NULL.
 * @retval     Non-NULL  The mutex has an associated backend state.
 * @retval     NULL      The mutex parameter is NULL.
 *
 * @note       This function is private to the PAL implementation.
 * @warning    Callers must not modify or free the returned state.
 */
void *pal_mutex_backend_state(struct pal_mutex *mutex)
{
    return (mutex != NULL) ? mutex->backend_state : NULL;
}
