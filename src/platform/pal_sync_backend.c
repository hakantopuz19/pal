/**
 * @file pal_sync_backend.c
 * @brief Platform-selected implementation of PAL backend operations.
 */

#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif

#if defined(_WIN32)
/* Windows backend implementation. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#elif _WIN32_WINNT < 0x0600
#error "PAL synchronization requires Windows Vista or newer."
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

#include "pal_error.h"
#include "pal_mutex.h"
#include "pal_backend.h"

typedef struct pal_windows_mutex
{
    SRWLOCK value;
} pal_windows_mutex_t;

typedef struct pal_windows_condition
{
    CONDITION_VARIABLE value;
} pal_windows_condition_t;

#elif defined(__linux__)
/* Linux backend implementation. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "pal_error.h"
#include "pal_mutex.h"
#include "pal_backend.h"

#else
#error "PAL backend is implemented for Windows and Linux only."
#endif

/* Shared backend entry points with platform-specific implementations. */
#if defined(_WIN32)
/**
 * @brief      Creates a native Windows SRW lock.
 *
 * @details    Allocates storage for an SRW lock, initializes it with the
 *             Windows API, and returns the allocation to the caller.
 *
 * @param[out] state  Receives the initialized native mutex pointer.
 *
 * @return     PAL_OK when initialization succeeds, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Mutex initialization succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The output parameter is NULL.
 * @retval     PAL_ERR_OUT_OF_MEMORY  Native mutex storage allocation failed.
 *
 * @note       SRW locks are available starting with Windows Vista.
 * @warning    The returned allocation must be destroyed with the matching
 *             backend function.
 */
#elif defined(__linux__)
/**
 * @brief      Creates and initializes a native pthread mutex.
 *
 * @details    Allocates storage for the pthread mutex, initializes it with
 *             default attributes, and returns the allocation to the caller.
 *
 * @param[out] state  Receives the initialized native mutex pointer.
 *
 * @return     PAL_OK when initialization succeeds, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Mutex initialization succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The output parameter is NULL.
 * @retval     PAL_ERR_OUT_OF_MEMORY  Native mutex storage allocation failed.
 * @retval     PAL_ERR_UNKNOWN        pthread_mutex_init() failed.
 *
 * @note       The native mutex is private to the PAL backend.
 * @warning    The returned allocation must be destroyed with the matching
 *             backend function.
 */
#endif
int32_t pal_sync_mutex_backend_create(void **state)
{
#if defined(_WIN32)
    pal_windows_mutex_t *mutex;

    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *state = NULL;

    mutex = (pal_windows_mutex_t *)malloc(sizeof(*mutex));
    if (mutex == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }
    InitializeSRWLock(&mutex->value);
    *state = mutex;
    return PAL_OK;
#elif defined(__linux__)
    pthread_mutex_t *mutex;

    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *state = NULL;

    mutex = (pthread_mutex_t *)malloc(sizeof(*mutex));
    if (mutex == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }
    if (pthread_mutex_init(mutex, NULL) != 0)
    {
        free(mutex);
        return PAL_ERR_UNKNOWN;
    }

    *state = mutex;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Frees the allocation holding a native Windows SRW lock.
 *
 * @details    Releases the PAL-owned storage for an SRW lock. Windows SRW
 *             locks do not require a separate native destruction call.
 *
 * @param[in]  state  Native mutex allocation to release.
 *
 * @return     PAL_OK when storage is released, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 Mutex storage was released.
 * @retval     PAL_ERR_NULL_POINTER   The state parameter is NULL.
 *
 * @note       This function does not verify whether the lock is held.
 * @warning    The lock must be unlocked and unused before its storage is freed.
 */
#elif defined(__linux__)
/**
 * @brief      Destroys and frees a native pthread mutex.
 *
 * @details    Calls pthread_mutex_destroy() before releasing its allocation.
 *             If pthread reports that the mutex is still busy, the allocation
 *             remains intact and an error is returned.
 *
 * @param[in]  state  Native mutex allocation to destroy.
 *
 * @return     PAL_OK when destruction succeeds, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Mutex destruction succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The state parameter is NULL.
 * @retval     PAL_ERR_BUSY           The native mutex is still in use.
 *
 * @note       pthread_mutex_destroy() is provided by the POSIX threads API.
 * @warning    The mutex must be unlocked and unused before destruction.
 */
#endif
int32_t pal_sync_mutex_backend_destroy(void *state)
{
#if defined(_WIN32)
    pal_windows_mutex_t *mutex = (pal_windows_mutex_t *)state;

    if (mutex == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    free(mutex);
    return PAL_OK;
#elif defined(__linux__)
    pthread_mutex_t *mutex = (pthread_mutex_t *)state;

    if (mutex == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (pthread_mutex_destroy(mutex) != 0)
    {
        return PAL_ERR_BUSY;
    }

    free(mutex);
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Acquires a native Windows SRW lock exclusively.
 *
 * @details    Calls AcquireSRWLockExclusive(), blocking the calling thread
 *             until exclusive ownership is available.
 *
 * @param[in]  state  Native mutex allocation to acquire.
 *
 * @return     PAL_OK when acquisition succeeds, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 Lock acquisition succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The state parameter is NULL.
 *
 * @note       The Windows API call has no error return value.
 * @warning    SRW locks are non-recursive and must not be acquired recursively.
 */
#elif defined(__linux__)
/**
 * @brief      Acquires a native pthread mutex.
 *
 * @details    Blocks the calling thread until pthread_mutex_lock() acquires
 *             the specified mutex.
 *
 * @param[in]  state  Native mutex allocation to acquire.
 *
 * @return     PAL_OK when acquisition succeeds, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Mutex acquisition succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The state parameter is NULL.
 * @retval     PAL_ERR_UNKNOWN        pthread_mutex_lock() failed.
 *
 * @note       This operation may block indefinitely.
 * @warning    Recursive acquisition is not supported by the default mutex.
 */
#endif
int32_t pal_sync_mutex_backend_lock(void *state)
{
#if defined(_WIN32)
    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    AcquireSRWLockExclusive(&((pal_windows_mutex_t *)state)->value);
    return PAL_OK;
#elif defined(__linux__)
    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    return (pthread_mutex_lock((pthread_mutex_t *)state) == 0)
        ? PAL_OK
        : PAL_ERR_UNKNOWN;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Releases a native Windows SRW lock.
 *
 * @details    Calls ReleaseSRWLockExclusive() to release exclusive ownership
 *             of the lock.
 *
 * @param[in]  state  Native mutex allocation to release.
 *
 * @return     PAL_OK when the lock is released, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 Lock release succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The state parameter is NULL.
 *
 * @note       The Windows API call has no error return value.
 * @warning    Only the owning thread may release an exclusively acquired lock.
 */
#elif defined(__linux__)
/**
 * @brief      Releases a native pthread mutex.
 *
 * @details    Calls pthread_mutex_unlock() to release the mutex to another
 *             waiting thread.
 *
 * @param[in]  state  Native mutex allocation to release.
 *
 * @return     PAL_OK when release succeeds, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                   Mutex release succeeded.
 * @retval     PAL_ERR_NULL_POINTER     The state parameter is NULL.
 * @retval     PAL_ERR_INVALID_STATE    pthread_mutex_unlock() rejected the
 *                                      operation.
 *
 * @note       POSIX requires the calling thread to own the mutex.
 * @warning    Unlocking a mutex not owned by the caller is invalid.
 */
#endif
int32_t pal_sync_mutex_backend_unlock(void *state)
{
#if defined(_WIN32)
    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    ReleaseSRWLockExclusive(&((pal_windows_mutex_t *)state)->value);
    return PAL_OK;
#elif defined(__linux__)
    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    return (pthread_mutex_unlock((pthread_mutex_t *)state) == 0)
        ? PAL_OK
        : PAL_ERR_INVALID_STATE;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Creates a native Windows condition variable.
 *
 * @details    Allocates storage for a condition variable, initializes it using
 *             InitializeConditionVariable(), and returns the allocation.
 *
 * @param[out] state  Receives the initialized condition variable pointer.
 *
 * @return     PAL_OK when initialization succeeds, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Condition initialization succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The output parameter is NULL.
 * @retval     PAL_ERR_OUT_OF_MEMORY  Condition storage allocation failed.
 *
 * @note       Windows condition variables are available starting with Windows
 *             Vista.
 * @warning    The returned allocation must be released after all waiters exit.
 */
#elif defined(__linux__)
/**
 * @brief      Creates and initializes a pthread condition variable.
 *
 * @details    Allocates storage for a pthread condition variable, initializes
 *             it with default attributes, and returns the allocation.
 *
 * @param[out] state  Receives the initialized condition variable pointer.
 *
 * @return     PAL_OK when initialization succeeds, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Condition initialization succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The output parameter is NULL.
 * @retval     PAL_ERR_OUT_OF_MEMORY  Condition storage allocation failed.
 * @retval     PAL_ERR_UNKNOWN        pthread_cond_init() failed.
 *
 * @note       The condition variable is private to the PAL backend.
 * @warning    The returned allocation must be destroyed by the matching
 *             backend function after all waiters have exited.
 */
#endif
int32_t pal_sync_condition_backend_create(void **state)
{
#if defined(_WIN32)
    pal_windows_condition_t *condition;

    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *state = NULL;

    condition = (pal_windows_condition_t *)malloc(sizeof(*condition));
    if (condition == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }
    InitializeConditionVariable(&condition->value);
    *state = condition;
    return PAL_OK;
#elif defined(__linux__)
    pthread_cond_t *condition;

    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *state = NULL;

    condition = (pthread_cond_t *)malloc(sizeof(*condition));
    if (condition == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }
    if (pthread_cond_init(condition, NULL) != 0)
    {
        free(condition);
        return PAL_ERR_UNKNOWN;
    }

    *state = condition;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Frees storage holding a Windows condition variable.
 *
 * @details    Releases the PAL-owned allocation. Windows condition variables
 *             do not require a separate native destruction call.
 *
 * @param[in]  state  Native condition allocation to release.
 *
 * @return     PAL_OK when storage is released, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 Condition storage was released.
 * @retval     PAL_ERR_NULL_POINTER   The state parameter is NULL.
 *
 * @note       No native cleanup operation is required for a condition variable.
 * @warning    No thread may be waiting on the condition when its storage is
 *             released.
 */
#elif defined(__linux__)
/**
 * @brief      Destroys and frees a pthread condition variable.
 *
 * @details    Calls pthread_cond_destroy() and frees the allocation only when
 *             no thread is blocked on the condition.
 *
 * @param[in]  state  Native condition allocation to destroy.
 *
 * @return     PAL_OK when destruction succeeds, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Condition destruction succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The state parameter is NULL.
 * @retval     PAL_ERR_BUSY           One or more threads are still waiting.
 *
 * @note       POSIX condition variables have no separate native free call.
 * @warning    Callers must ensure no thread is waiting on the condition.
 */
#endif
int32_t pal_sync_condition_backend_destroy(void *state)
{
#if defined(_WIN32)
    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    free(state);
    return PAL_OK;
#elif defined(__linux__)
    pthread_cond_t *condition = (pthread_cond_t *)state;

    if (condition == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    if (pthread_cond_destroy(condition) != 0)
    {
        return PAL_ERR_BUSY;
    }

    free(condition);
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Waits on a Windows condition and releases the associated mutex.
 *
 * @details    Calls SleepConditionVariableSRW(), which atomically releases the
 *             SRW lock while waiting and reacquires it before returning.
 *             UINT32_MAX is translated to INFINITE.
 *
 * @param[in]  condition   Native condition variable.
 * @param[in]  mutex       PAL mutex guarding the condition.
 * @param[in]  timeout_ms  Timeout in milliseconds or UINT32_MAX for infinity.
 *
 * @return     PAL_OK when awakened, otherwise a negative PAL error code.
 * @retval     PAL_OK                 The condition wait returned successfully.
 * @retval     PAL_ERR_NULL_POINTER   A condition or mutex state is NULL.
 * @retval     PAL_ERR_TIMEOUT        The finite wait timed out.
 * @retval     PAL_ERR_UNKNOWN        The Windows wait API failed.
 *
 * @note       A successful return does not imply the waited-on predicate is
 *             true.
 * @warning    The caller must re-check its condition predicate after waking.
 */
#elif defined(__linux__)
/**
 * @brief      Waits on a pthread condition while releasing the PAL mutex.
 *
 * @details    Uses pthread_cond_wait() for an infinite timeout and
 *             pthread_cond_timedwait() for finite timeouts. The timed wait
 *             uses an absolute CLOCK_REALTIME deadline, as required by the
 *             default pthread condition-variable clock.
 *
 * @param[in]  condition   Native condition variable.
 * @param[in]  mutex       PAL mutex guarding the associated condition.
 * @param[in]  timeout_ms  Timeout in milliseconds or UINT32_MAX for infinity.
 *
 * @return     PAL_OK when awakened, otherwise a negative PAL error code.
 * @retval     PAL_OK                 The condition wait returned successfully.
 * @retval     PAL_ERR_NULL_POINTER   A condition or mutex state is NULL.
 * @retval     PAL_ERR_TIMEOUT        The finite wait timed out.
 * @retval     PAL_ERR_UNKNOWN        Clock retrieval or pthread wait failed.
 *
 * @note       The native mutex is reacquired before this function returns.
 * @warning    The caller must re-check its condition predicate after waking.
 */
#endif
int32_t pal_sync_condition_backend_wait(
    void *condition,
    pal_mutex_t *mutex,
    uint32_t timeout_ms)
{
#if defined(_WIN32)
    DWORD duration;
    BOOL result;

    void *native_mutex = pal_mutex_backend_state(mutex);

    if (condition == NULL || native_mutex == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    duration = (timeout_ms == UINT32_MAX) ? INFINITE : (DWORD)timeout_ms;
    result = SleepConditionVariableSRW(
        &((pal_windows_condition_t *)condition)->value,
        &((pal_windows_mutex_t *)native_mutex)->value,
        duration,
        0u);

    if (result != 0)
    {
        return PAL_OK;
    }
    return (GetLastError() == ERROR_TIMEOUT) ? PAL_ERR_TIMEOUT : PAL_ERR_UNKNOWN;
#elif defined(__linux__)
    struct timespec deadline;
    pthread_cond_t *native_condition = (pthread_cond_t *)condition;
    void *native_mutex = pal_mutex_backend_state(mutex);
    int result;

    if (native_condition == NULL || native_mutex == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    if (timeout_ms == UINT32_MAX)
    {
        result = pthread_cond_wait(native_condition, (pthread_mutex_t *)native_mutex);
    }
    else
    {
        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        {
            return PAL_ERR_UNKNOWN;
        }

        deadline.tv_sec += (time_t)(timeout_ms / 1000u);
        deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L)
        {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }

        result = pthread_cond_timedwait(
            native_condition,
            (pthread_mutex_t *)native_mutex,
            &deadline);
    }

    if (result == 0)
    {
        return PAL_OK;
    }
    return (result == ETIMEDOUT) ? PAL_ERR_TIMEOUT : PAL_ERR_UNKNOWN;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Wakes one thread waiting on a Windows condition variable.
 *
 * @details    Calls WakeConditionVariable() to make one waiting thread
 *             eligible to resume.
 *
 * @param[in]  condition  Native condition variable to signal.
 *
 * @return     PAL_OK when a waiter is signaled, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 Wake signal issued.
 * @retval     PAL_ERR_NULL_POINTER   The condition parameter is NULL.
 *
 * @note       The caller should hold the mutex protecting the condition.
 * @warning    A wake signal does not guarantee that the condition predicate
 *             will be true when the waiter resumes.
 */
#elif defined(__linux__)
/**
 * @brief      Signals one thread waiting on a pthread condition.
 *
 * @details    Calls pthread_cond_signal() so that one thread waiting on the
 *             condition becomes eligible to resume.
 *
 * @param[in]  condition  Native condition variable to signal.
 *
 * @return     PAL_OK when the signal succeeds, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 Signal succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The condition parameter is NULL.
 * @retval     PAL_ERR_UNKNOWN        pthread_cond_signal() failed.
 *
 * @note       The caller should hold the mutex that protects the condition.
 * @warning    A signal does not guarantee that a waiter will observe its
 *             predicate as true when it resumes.
 */
#endif
int32_t pal_sync_condition_backend_signal(void *condition)
{
#if defined(_WIN32)
    if (condition == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    WakeConditionVariable(&((pal_windows_condition_t *)condition)->value);
    return PAL_OK;
#elif defined(__linux__)
    if (condition == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    return (pthread_cond_signal((pthread_cond_t *)condition) == 0)
        ? PAL_OK
        : PAL_ERR_UNKNOWN;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Reads the Windows monotonic tick count in milliseconds.
 *
 * @details    Calls GetTickCount64() to obtain a monotonic millisecond count
 *             used when calculating finite queue wait durations.
 *
 * @param[out] time_ms  Receives the monotonic timestamp in milliseconds.
 *
 * @return     PAL_OK when the tick count is read, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                 Timestamp retrieval succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The output parameter is NULL.
 *
 * @note       The value is not a calendar timestamp.
 * @warning    Use differences between readings to measure elapsed time.
 */
#elif defined(__linux__)
/**
 * @brief      Reads the monotonic system clock in milliseconds.
 *
 * @details    Calls clock_gettime(CLOCK_MONOTONIC) and converts the result to
 *             milliseconds for timeout duration calculations.
 *
 * @param[out] time_ms  Receives the monotonic timestamp in milliseconds.
 *
 * @return     PAL_OK when the clock is read, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                 Timestamp retrieval succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The output parameter is NULL.
 * @retval     PAL_ERR_UNKNOWN        clock_gettime() failed.
 *
 * @note       The returned value is for elapsed-time measurement and is not
 *             calendar time.
 * @warning    The value must not be interpreted as a wall-clock timestamp.
 */
#endif
int32_t pal_sync_monotonic_time_ms(uint64_t *time_ms)
{
#if defined(_WIN32)
    if (time_ms == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    *time_ms = (uint64_t)GetTickCount64();
    return PAL_OK;
#elif defined(__linux__)
    struct timespec now;

    if (time_ms == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return PAL_ERR_UNKNOWN;
    }

    *time_ms = ((uint64_t)now.tv_sec * 1000u) + ((uint64_t)now.tv_nsec / 1000000u);
    return PAL_OK;
#endif
}
