/**
 * @file pal_thread.c
 * @brief Platform-independent thread lifecycle API implementation.
 */

#include <stdlib.h>

#include "pal_error.h"
#include "pal_thread.h"
#include "pal_backend.h"

struct pal_thread
{
    void *backend_state;
};

/**
 * @brief      Creates a joinable PAL thread.
 *
 * @details    Allocates the public thread object and delegates native thread
 *             creation to the selected platform backend. The entry function
 *             receives the supplied argument and returns an opaque result
 *             that can be retrieved by joining the thread.
 *
 * @param[in]  entry     Function executed by the new thread.
 * @param[in]  argument  Argument passed to the entry function.
 * @param[out] thread    Receives the created thread handle.
 *
 * @return     PAL_OK when the thread is created, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                   Thread creation succeeded.
 * @retval     PAL_ERR_NULL_POINTER     The entry function or output parameter
 *                                      is NULL.
 * @retval     PAL_ERR_OUT_OF_MEMORY    PAL thread state allocation failed.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED The platform could not create a
 *                                        native thread.
 * @retval     Other PAL_ERR_*          The backend reported a creation error.
 *
 * @note       The thread is joinable until it is joined or detached.
 * @warning    The argument object must remain valid until the entry function
 *             has finished using it.
 */
int32_t pal_thread_create(
    pal_thread_fn_t entry,
    void *argument,
    pal_thread_t **thread)
{
    pal_thread_t *created;
    int32_t status;

    if (thread == NULL || entry == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    *thread = NULL;
    created = (pal_thread_t *)calloc(1u, sizeof(*created));
    if (created == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }

    status = pal_thread_backend_create(entry, argument, &created->backend_state);
    if (status != PAL_OK)
    {
        free(created);
        return status;
    }

    *thread = created;
    return PAL_OK;
}

/**
 * @brief      Waits for a joinable thread to terminate.
 *
 * @details    Blocks until the native thread exits, retrieves its pointer
 *             result when requested, and releases the PAL thread handle.
 *
 * @param[in]  thread  Joinable thread to wait for.
 * @param[out] result  Receives the entry function's return value; may be NULL.
 *
 * @return     PAL_OK when the thread is joined, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                   Thread was joined successfully.
 * @retval     PAL_ERR_NULL_POINTER     The thread parameter is NULL.
 * @retval     PAL_ERR_INVALID_STATE    The backend could not join the thread.
 * @retval     Other PAL_ERR_*          The backend reported a join error.
 *
 * @note       The result is the pointer returned by the entry function or
 *             supplied to pal_thread_exit().
 * @warning    After a successful join, the thread handle is invalid and must
 *             not be used again.
 */
int32_t pal_thread_join(pal_thread_t *thread, void **result)
{
    int32_t status;

    if (thread == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_thread_backend_join(thread->backend_state, result);
    if (status != PAL_OK)
    {
        return status;
    }

    free(thread);
    return PAL_OK;
}

/**
 * @brief      Detaches a thread from join-based synchronization.
 *
 * @details    Marks the native thread as detached and releases the PAL thread
 *             handle. Native resources are reclaimed automatically when the
 *             thread terminates.
 *
 * @param[in]  thread  Joinable thread to detach.
 *
 * @return     PAL_OK when the thread is detached, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                   Thread was detached successfully.
 * @retval     PAL_ERR_NULL_POINTER     The thread parameter is NULL.
 * @retval     PAL_ERR_INVALID_STATE    The backend could not detach the thread.
 * @retval     Other PAL_ERR_*          The backend reported a detach error.
 *
 * @note       A detached thread's return value cannot be retrieved.
 * @warning    After a successful detach, the thread handle is invalid and must
 *             not be used again.
 */
int32_t pal_thread_detach(pal_thread_t *thread)
{
    int32_t status;

    if (thread == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_thread_backend_detach(thread->backend_state);
    if (status != PAL_OK)
    {
        return status;
    }

    free(thread);
    return PAL_OK;
}

/**
 * @brief      Terminates the calling thread with a pointer result.
 *
 * @details    Delegates termination to the native thread backend. When invoked
 *             from a joinable PAL thread, the supplied result is returned to
 *             the thread that joins it.
 *
 * @param[in]  result  Opaque result value supplied to pal_thread_join().
 *
 * @return     This function does not return to the calling thread.
 *
 * @note       The result is only retrievable when the caller is a joinable PAL
 *             thread and another thread joins it.
 * @warning    Calling this function terminates the current thread immediately;
 *             stack-based cleanup after the call will not run.
 */
void pal_thread_exit(void *result)
{
    pal_thread_backend_exit(result);
}
