/**
 * @file pal_thread_backend.c
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
#error "PAL threads require Windows Vista or newer."
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

#include "pal_error.h"
#include "pal_backend.h"
#include "pal_thread.h"

typedef struct pal_windows_thread_context
{
    pal_thread_fn_t entry;
    void *argument;
    void *result;
    volatile LONG references;
} pal_windows_thread_context_t;

typedef struct pal_windows_thread_state
{
    HANDLE handle;
    pal_windows_thread_context_t *context;
} pal_windows_thread_state_t;

static INIT_ONCE g_pal_thread_tls_once = INIT_ONCE_STATIC_INIT;
static DWORD g_pal_thread_tls_index = FLS_OUT_OF_INDEXES;

/**
 * @brief      Releases one reference to a Windows thread context.
 *
 * @details    Decrements the interlocked reference count and frees the context
 *             after both the PAL handle and running thread release ownership.
 *
 * @param[in]  context  Thread context whose reference is released.
 *
 * @return     This function does not return a value.
 *
 * @note       The reference count is updated atomically for thread safety.
 * @warning    The context pointer must refer to a live allocated context.
 */
static void pal_windows_context_release(pal_windows_thread_context_t *context)
{
    if (InterlockedDecrement(&context->references) == 0)
    {
        free(context);
    }
}

/**
 * @brief      Releases the running-thread reference when a Windows thread exits.
 *
 * @details    Fiber-local storage invokes this callback during thread
 *             termination when the PAL thread exits before its trampoline
 *             returns normally.
 *
 * @param[in]  value  Thread context whose worker reference must be released.
 *
 * @return     This callback does not return a value.
 *
 * @note       The reference count is updated atomically.
 * @warning    The value must refer to a live PAL thread context.
 */
static VOID CALLBACK pal_windows_thread_fls_cleanup(PVOID value)
{
    if (value != NULL)
    {
        pal_windows_context_release((pal_windows_thread_context_t *)value);
    }
}

/**
 * @brief      Initializes the process-wide Windows fiber-local storage slot.
 *
 * @details    Allocates an FLS index with a cleanup callback so PAL thread
 *             state is released even when pal_thread_exit() terminates the
 *             thread before its entry trampoline returns.
 *
 * @param[in]  once       Init-once object supplied by Windows.
 * @param[in]  parameter  Reserved callback argument.
 * @param[out] context    Receives reserved callback context data.
 *
 * @return     TRUE when FLS allocation succeeds; otherwise FALSE.
 *
 * @note       Windows invokes this callback at most once for this InitOnce
 *             object.
 * @warning    The allocated FLS index remains process-wide for program life.
 */
static BOOL CALLBACK pal_windows_thread_tls_initialize(
    PINIT_ONCE once,
    PVOID parameter,
    PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    g_pal_thread_tls_index = FlsAlloc(pal_windows_thread_fls_cleanup);
    return (g_pal_thread_tls_index != FLS_OUT_OF_INDEXES);
}

/**
 * @brief      Runs a user callback on a native Windows thread.
 *
 * @details    Stores the thread context in TLS, runs the user callback,
 *             records its pointer result, clears TLS, and releases the
 *             running-thread reference to the context.
 *
 * @param[in]  parameter  Allocated callback context.
 *
 * @return     Zero as the native DWORD exit code; the PAL pointer result is
 *             stored separately in the context.
 *
 * @note       The PAL thread result is retrieved by pal_thread_backend_join().
 * @warning    The startup context is owned by this trampoline.
 */
static DWORD WINAPI pal_windows_thread_entry(LPVOID parameter)
{
    pal_windows_thread_context_t *thread_context =
        (pal_windows_thread_context_t *)parameter;

    if (FlsSetValue(g_pal_thread_tls_index, thread_context) == 0)
    {
        thread_context->result = NULL;
        pal_windows_context_release(thread_context);
        return 0u;
    }

    thread_context->result = thread_context->entry(thread_context->argument);
    (void)FlsSetValue(g_pal_thread_tls_index, NULL);
    pal_windows_context_release(thread_context);
    return 0u;
}

#elif defined(__linux__)
/* Linux backend implementation. */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdlib.h>

#include "pal_error.h"
#include "pal_backend.h"
#include "pal_thread.h"

typedef struct pal_pthread_context
{
    pal_thread_fn_t entry;
    void *argument;
} pal_pthread_context_t;

typedef struct pal_pthread_state
{
    pthread_t handle;
} pal_pthread_state_t;

/**
 * @brief      Runs the user entry point for a newly created pthread.
 *
 * @details    Copies the callback and argument from the startup context,
 *             releases that context, then returns the callback result to the
 *             pthread runtime.
 *
 * @param[in]  argument  Allocated callback and argument context.
 *
 * @return     The pointer returned by the user entry function.
 *
 * @note       The startup context is owned by this trampoline.
 * @warning    The callback argument must remain valid until callback completion.
 */
static void *pal_pthread_entry(void *argument)
{
    pal_pthread_context_t *context = (pal_pthread_context_t *)argument;
    pal_thread_fn_t entry = context->entry;
    void *user_argument = context->argument;

    free(context);
    return entry(user_argument);
}

#else
#error "PAL backend is implemented for Windows and Linux only."
#endif

/* Shared backend entry points with platform-specific implementations. */
#if defined(_WIN32)
/**
 * @brief      Creates a native Windows thread and PAL backend state.
 *
 * @details    Initializes TLS support, allocates a thread state and a
 *             reference-counted callback context, then starts the native
 *             thread.
 *
 * @param[in]  entry     User callback to run.
 * @param[in]  argument  Callback argument.
 * @param[out] state     Receives the native Windows thread state allocation.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                       Thread creation succeeded.
 * @retval     PAL_ERR_NULL_POINTER         An input or output pointer is NULL.
 * @retval     PAL_ERR_OUT_OF_MEMORY        A PAL allocation failed.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED   TLS or native thread creation failed.
 *
 * @note       The created native thread is joinable.
 * @warning    The callback argument must remain valid until callback completion.
 */
#elif defined(__linux__)
/**
 * @brief      Creates a native pthread and its PAL backend state.
 *
 * @details    Allocates independent thread state and callback context, then
 *             starts a joinable POSIX thread.
 *
 * @param[in]  entry     User callback to run.
 * @param[in]  argument  Callback argument.
 * @param[out] state     Receives the native pthread state allocation.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                       pthread creation succeeded.
 * @retval     PAL_ERR_NULL_POINTER         An input or output pointer is NULL.
 * @retval     PAL_ERR_OUT_OF_MEMORY        A state allocation failed.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED   pthread_create() failed.
 *
 * @note       The created pthread is joinable.
 * @warning    Do not release the callback argument until the entry function
 *             has completed.
 */
#endif
int32_t pal_thread_backend_create(
    pal_thread_fn_t entry,
    void *argument,
    void **state)
{
#if defined(_WIN32)
    pal_windows_thread_state_t *thread_state;
    pal_windows_thread_context_t *thread_context;
    DWORD thread_id;

    if (entry == NULL || state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *state = NULL;

    if (InitOnceExecuteOnce(
            &g_pal_thread_tls_once,
            pal_windows_thread_tls_initialize,
            NULL,
            NULL) == 0)
    {
        return PAL_ERR_RESOURCE_EXHAUSTED;
    }

    thread_state = (pal_windows_thread_state_t *)malloc(sizeof(*thread_state));
    thread_context = (pal_windows_thread_context_t *)calloc(1u, sizeof(*thread_context));
    if (thread_state == NULL || thread_context == NULL)
    {
        free(thread_state);
        free(thread_context);
        return PAL_ERR_OUT_OF_MEMORY;
    }

    thread_context->entry = entry;
    thread_context->argument = argument;
    thread_context->references = 2;
    thread_state->context = thread_context;
    thread_state->handle = CreateThread(
        NULL,
        0u,
        pal_windows_thread_entry,
        thread_context,
        0u,
        &thread_id);

    if (thread_state->handle == NULL)
    {
        free(thread_context);
        free(thread_state);
        return PAL_ERR_RESOURCE_EXHAUSTED;
    }

    *state = thread_state;
    return PAL_OK;
#elif defined(__linux__)
    pal_pthread_state_t *thread_state;
    pal_pthread_context_t *context;
    int result;

    if (entry == NULL || state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *state = NULL;

    thread_state = (pal_pthread_state_t *)malloc(sizeof(*thread_state));
    context = (pal_pthread_context_t *)malloc(sizeof(*context));
    if (thread_state == NULL || context == NULL)
    {
        free(thread_state);
        free(context);
        return PAL_ERR_OUT_OF_MEMORY;
    }

    context->entry = entry;
    context->argument = argument;
    result = pthread_create(&thread_state->handle, NULL, pal_pthread_entry, context);
    if (result != 0)
    {
        free(context);
        free(thread_state);
        return PAL_ERR_RESOURCE_EXHAUSTED;
    }

    *state = thread_state;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Joins a native Windows thread and releases its backend state.
 *
 * @details    Waits indefinitely for the native thread, copies its pointer
 *             result when requested, and releases both the native thread
 *             handle and the PAL-owned thread state.
 *
 * @param[in]  state   Native Windows thread state.
 * @param[out] result  Receives the callback result; may be NULL.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   Thread wait and handle cleanup succeeded.
 * @retval     PAL_ERR_NULL_POINTER     The state parameter is NULL.
 * @retval     PAL_ERR_INVALID_STATE    The Windows wait operation failed.
 * @retval     PAL_ERR_UNKNOWN          The native handle could not be closed.
 *
 * @note       The result is available after the thread signals termination.
 * @warning    The thread must not have been joined or detached previously.
 */
#elif defined(__linux__)
/**
 * @brief      Joins a native pthread and releases its backend state.
 *
 * @details    Waits for the native thread to finish, optionally returns its
 *             callback result, and releases the state allocation.
 *
 * @param[in]  state   Native pthread state allocation.
 * @param[out] result  Receives the thread result; may be NULL.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 pthread_join() succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The state parameter is NULL.
 * @retval     PAL_ERR_INVALID_STATE  pthread_join() rejected the thread.
 *
 * @note       pthread_join() is a blocking operation.
 * @warning    The thread must not have been detached or joined previously.
 */
#endif
int32_t pal_thread_backend_join(void *state, void **result)
{
#if defined(_WIN32)
    pal_windows_thread_state_t *thread_state =
        (pal_windows_thread_state_t *)state;
    DWORD wait_result;

    if (thread_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    wait_result = WaitForSingleObject(thread_state->handle, INFINITE);
    if (wait_result != WAIT_OBJECT_0)
    {
        return PAL_ERR_INVALID_STATE;
    }

    if (result != NULL)
    {
        *result = thread_state->context->result;
    }
    if (CloseHandle(thread_state->handle) == 0)
    {
        return PAL_ERR_UNKNOWN;
    }

    pal_windows_context_release(thread_state->context);
    free(thread_state);
    return PAL_OK;
#elif defined(__linux__)
    pal_pthread_state_t *thread_state = (pal_pthread_state_t *)state;
    void *thread_result = NULL;

    if (thread_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    if (pthread_join(thread_state->handle, &thread_result) != 0)
    {
        return PAL_ERR_INVALID_STATE;
    }

    if (result != NULL)
    {
        *result = thread_result;
    }
    free(thread_state);
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Detaches a native Windows thread and releases its PAL handle.
 *
 * @details    Closes the native thread handle and releases the PAL's context
 *             reference. The running thread retains its own reference until
 *             its callback has completed.
 *
 * @param[in]  state  Native Windows thread state.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   Native thread handle was closed.
 * @retval     PAL_ERR_NULL_POINTER     The state parameter is NULL.
 * @retval     PAL_ERR_UNKNOWN          CloseHandle() failed.
 *
 * @note       The detached thread result cannot be retrieved.
 * @warning    The thread must not have been joined or detached previously.
 */
#elif defined(__linux__)
/**
 * @brief      Detaches a native pthread and releases its backend state.
 *
 * @details    Calls pthread_detach() so the runtime reclaims native resources
 *             when the thread exits, then frees the PAL backend state.
 *
 * @param[in]  state  Native pthread state allocation.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 pthread_detach() succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The state parameter is NULL.
 * @retval     PAL_ERR_INVALID_STATE  pthread_detach() rejected the thread.
 *
 * @note       The detached thread result cannot be retrieved.
 * @warning    The thread must not have been joined or detached previously.
 */
#endif
int32_t pal_thread_backend_detach(void *state)
{
#if defined(_WIN32)
    pal_windows_thread_state_t *thread_state =
        (pal_windows_thread_state_t *)state;

    if (thread_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (CloseHandle(thread_state->handle) == 0)
    {
        return PAL_ERR_UNKNOWN;
    }

    pal_windows_context_release(thread_state->context);
    free(thread_state);
    return PAL_OK;
#elif defined(__linux__)
    pal_pthread_state_t *thread_state = (pal_pthread_state_t *)state;

    if (thread_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    if (pthread_detach(thread_state->handle) != 0)
    {
        return PAL_ERR_INVALID_STATE;
    }

    free(thread_state);
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Terminates the calling Windows thread with a pointer result.
 *
 * @details    Stores the result in the current PAL thread context when
 *             available, then calls ExitThread(). Threads not created by the
 *             PAL terminate with a native exit code of zero.
 *
 * @param[in]  result  Pointer result returned to pal_thread_join().
 *
 * @return     This function does not return to the calling thread.
 *
 * @note       The PAL result is retrievable only for joinable PAL threads.
 * @warning    The calling thread is terminated immediately.
 */
#elif defined(__linux__)
/**
 * @brief      Terminates the calling pthread with a pointer result.
 *
 * @details    Calls pthread_exit() so the result becomes available to a thread
 *             that joins the caller.
 *
 * @param[in]  result  Pointer result to return to the joining thread.
 *
 * @return     This function does not return.
 *
 * @note       POSIX thread cleanup handlers and thread-specific destructors
 *             run according to the pthread runtime rules.
 * @warning    The calling thread terminates immediately.
 */
#endif
void pal_thread_backend_exit(void *result)
{
#if defined(_WIN32)
    pal_windows_thread_context_t *thread_context = NULL;

    if (g_pal_thread_tls_index != FLS_OUT_OF_INDEXES)
    {
        thread_context =
            (pal_windows_thread_context_t *)FlsGetValue(g_pal_thread_tls_index);
    }
    if (thread_context != NULL)
    {
        thread_context->result = result;
    }

    ExitThread(0u);
#elif defined(__linux__)
    pthread_exit(result);
#endif
}
