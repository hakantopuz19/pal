/**
 * @file pal_time_backend.c
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
#error "PAL time backend requires Windows Vista or newer."
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <float.h>
#include <stdint.h>

#include "pal_error.h"
#include "pal_backend.h"

/**
 * @brief      Converts a performance counter to nanoseconds.
 *
 * @details    Uses long double scaling to avoid overflowing an intermediate
 *             integer product when converting counter ticks.
 *
 * @param[in]  counter_value  Current performance counter value.
 * @param[in]  frequency      Performance counter ticks per second.
 * @param[out] time_ns        Receives the scaled nanosecond value.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Counter converted.
 * @retval     PAL_ERR_NULL_POINTER   Output pointer is NULL.
 * @retval     PAL_ERR_OUT_OF_RANGE   Converted value exceeds uint64_t.
 *
 * @note       QueryPerformanceCounter is monotonic for elapsed-time use.
 * @warning    The value has no relation to the Unix epoch.
 */
static int32_t pal_windows_counter_to_ns(
    LARGE_INTEGER counter_value,
    LARGE_INTEGER frequency,
    uint64_t *time_ns)
{
    long double converted;

    if (time_ns == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (counter_value.QuadPart < 0 || frequency.QuadPart <= 0)
    {
        return PAL_ERR_UNKNOWN;
    }

    converted = ((long double)counter_value.QuadPart * 1000000000.0L) /
                (long double)frequency.QuadPart;
    if (converted >= (long double)UINT64_MAX)
    {
        return PAL_ERR_OUT_OF_RANGE;
    }

    *time_ns = (uint64_t)converted;
    return PAL_OK;
}
#elif defined(__linux__)
/* Linux backend implementation. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "pal_error.h"
#include "pal_backend.h"

/**
 * @brief      Converts a POSIX timespec to an unsigned nanosecond value.
 *
 * @details    Validates that seconds are nonnegative and that multiplying the
 *             second count by one billion fits in uint64_t.
 *
 * @param[in]  value     POSIX timestamp to convert.
 * @param[out] time_ns  Receives the nanosecond timestamp.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Conversion succeeded.
 * @retval     PAL_ERR_NULL_POINTER   An input or output pointer is NULL.
 * @retval     PAL_ERR_OUT_OF_RANGE   Timestamp is negative or overflows.
 *
 * @note       Sub-second nanoseconds are preserved.
 * @warning    The timespec must contain a normalized nanosecond field.
 */
static int32_t pal_linux_timespec_to_ns(
    const struct timespec *value,
    uint64_t *time_ns)
{
    uint64_t seconds;

    if (value == NULL || time_ns == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (value->tv_sec < 0 || value->tv_nsec < 0 || value->tv_nsec >= 1000000000L)
    {
        return PAL_ERR_OUT_OF_RANGE;
    }

    seconds = (uint64_t)value->tv_sec;
    if (seconds > (UINT64_MAX - (uint64_t)value->tv_nsec) / UINT64_C(1000000000))
    {
        return PAL_ERR_OUT_OF_RANGE;
    }

    *time_ns = seconds * UINT64_C(1000000000) + (uint64_t)value->tv_nsec;
    return PAL_OK;
}
#else
#error "PAL backend is implemented for Windows and Linux only."
#endif

/* Shared backend entry points with platform-specific implementations. */
#if defined(_WIN32)
/**
 * @brief      Reads the Windows monotonic performance counter in nanoseconds.
 *
 * @details    Queries QueryPerformanceCounter() and QueryPerformanceFrequency()
 *             and scales the counter to nanoseconds.
 *
 * @param[out] time_ns  Receives the monotonic timestamp.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Timestamp retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        Performance counter query failed.
 * @retval     PAL_ERR_OUT_OF_RANGE   Converted timestamp does not fit.
 *
 * @note       The counter is monotonic and unaffected by wall-clock changes.
 * @warning    Its origin is unspecified; do not interpret it as UTC.
 */
#elif defined(__linux__)
/**
 * @brief      Reads the Linux monotonic clock in nanoseconds.
 *
 * @details    Calls clock_gettime(CLOCK_MONOTONIC) and converts the resulting
 *             timestamp to nanoseconds.
 *
 * @param[out] time_ns  Receives the monotonic timestamp.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Timestamp retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        clock_gettime() failed.
 * @retval     PAL_ERR_OUT_OF_RANGE   Timestamp cannot fit in uint64_t.
 *
 * @note       The clock is unaffected by wall-clock adjustments.
 * @warning    Its origin is unspecified and must not be used as UTC time.
 */
#endif
int32_t pal_time_backend_monotonic_ns(uint64_t *time_ns)
{
#if defined(_WIN32)
    LARGE_INTEGER counter_value;
    LARGE_INTEGER frequency;

    if (time_ns == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (QueryPerformanceFrequency(&frequency) == 0 ||
        QueryPerformanceCounter(&counter_value) == 0)
    {
        return PAL_ERR_UNKNOWN;
    }

    return pal_windows_counter_to_ns(counter_value, frequency, time_ns);
#elif defined(__linux__)
    struct timespec now;

    if (time_ns == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return PAL_ERR_UNKNOWN;
    }
    return pal_linux_timespec_to_ns(&now, time_ns);
#endif
}

#if defined(_WIN32)
/**
 * @brief      Reads Windows UTC time in nanoseconds since the Unix epoch.
 *
 * @details    Converts FILETIME's 100-nanosecond intervals since 1601 to
 *             nanoseconds since 1970-01-01 00:00:00 UTC.
 *
 * @param[out] time_ns  Receives the UTC Unix timestamp.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Timestamp retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Output pointer is NULL.
 * @retval     PAL_ERR_OUT_OF_RANGE   FILETIME predates the Unix epoch.
 *
 * @note       FILETIME has 100-nanosecond resolution.
 * @warning    Wall-clock time may change; use monotonic time for durations.
 */
#elif defined(__linux__)
/**
 * @brief      Reads Linux UTC wall-clock time in nanoseconds since the epoch.
 *
 * @details    Calls clock_gettime(CLOCK_REALTIME) and converts the result to
 *             nanoseconds since 1970-01-01 00:00:00 UTC.
 *
 * @param[out] time_ns  Receives the UTC Unix timestamp.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Timestamp retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        clock_gettime() failed.
 * @retval     PAL_ERR_OUT_OF_RANGE   Timestamp is negative or overflows.
 *
 * @note       The wall clock may change due to system clock updates.
 * @warning    Do not use this clock for elapsed-time measurement.
 */
#endif
int32_t pal_time_backend_utc_ns(uint64_t *time_ns)
{
#if defined(_WIN32)
    const uint64_t windows_to_unix_epoch_100ns = UINT64_C(116444736000000000);
    FILETIME file_time;
    ULARGE_INTEGER ticks;
    uint64_t unix_ticks;

    if (time_ns == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    GetSystemTimeAsFileTime(&file_time);
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    if (ticks.QuadPart < windows_to_unix_epoch_100ns)
    {
        return PAL_ERR_OUT_OF_RANGE;
    }

    unix_ticks = ticks.QuadPart - windows_to_unix_epoch_100ns;
    if (unix_ticks > UINT64_MAX / 100u)
    {
        return PAL_ERR_OUT_OF_RANGE;
    }

    *time_ns = unix_ticks * 100u;
    return PAL_OK;
#elif defined(__linux__)
    struct timespec now;

    if (time_ns == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
    {
        return PAL_ERR_UNKNOWN;
    }
    return pal_linux_timespec_to_ns(&now, time_ns);
#endif
}

#if defined(_WIN32)
/**
 * @brief      Sleeps using a Windows waitable timer.
 *
 * @details    Converts the relative nanosecond duration to negative 100-
 *             nanosecond timer units, rounds up to avoid returning early, and
 *             waits until the timer is signaled.
 *
 * @param[in]  duration_ns  Requested sleep duration in nanoseconds.
 *
 * @return     PAL_OK on completion or a negative PAL error code.
 * @retval     PAL_OK                 Timer elapsed.
 * @retval     PAL_ERR_OUT_OF_RANGE   Duration exceeds waitable timer range.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED Timer handle creation failed.
 * @retval     PAL_ERR_UNKNOWN        Timer setup or wait failed.
 *
 * @note       Timer and scheduler resolution may extend the actual sleep.
 * @warning    This function does not provide hard real-time guarantees.
 */
#elif defined(__linux__)
/**
 * @brief      Sleeps for a relative duration using nanosleep().
 *
 * @details    Splits very long waits into time_t-safe chunks and resumes
 *             interrupted waits using the remaining duration.
 *
 * @param[in]  duration_ns  Requested duration in nanoseconds.
 *
 * @return     PAL_OK on completion or a negative PAL error code.
 * @retval     PAL_OK                 Requested duration elapsed.
 * @retval     PAL_ERR_UNKNOWN        nanosleep() failed for a reason other
 *                                    than EINTR.
 *
 * @note       The scheduler may resume the thread later than requested.
 * @warning    This function does not provide hard real-time guarantees.
 */
#endif
int32_t pal_time_backend_sleep_ns(uint64_t duration_ns)
{
#if defined(_WIN32)
    HANDLE timer;
    LARGE_INTEGER due_time;
    uint64_t timer_units;
    DWORD wait_status;

    if (duration_ns == 0u)
    {
        return PAL_OK;
    }

    timer_units = duration_ns / 100u;
    if ((duration_ns % 100u) != 0u)
    {
        timer_units++;
    }
    if (timer_units > (uint64_t)INT64_MAX)
    {
        return PAL_ERR_OUT_OF_RANGE;
    }

    timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    if (timer == NULL)
    {
        return PAL_ERR_RESOURCE_EXHAUSTED;
    }

    due_time.QuadPart = -(LONGLONG)timer_units;
    if (SetWaitableTimer(timer, &due_time, 0, NULL, NULL, FALSE) == 0)
    {
        (void)CloseHandle(timer);
        return PAL_ERR_UNKNOWN;
    }

    wait_status = WaitForSingleObject(timer, INFINITE);
    (void)CloseHandle(timer);
    return (wait_status == WAIT_OBJECT_0) ? PAL_OK : PAL_ERR_UNKNOWN;
#elif defined(__linux__)
    const uint64_t max_chunk_seconds = (uint64_t)INT32_MAX;

    while (duration_ns > 0u)
    {
        struct timespec requested;
        struct timespec remaining;
        uint64_t seconds = duration_ns / UINT64_C(1000000000);
        uint64_t nanoseconds = duration_ns % UINT64_C(1000000000);

        if (seconds > max_chunk_seconds)
        {
            seconds = max_chunk_seconds;
            nanoseconds = 0u;
        }

        requested.tv_sec = (time_t)seconds;
        requested.tv_nsec = (long)nanoseconds;
        while (nanosleep(&requested, &remaining) != 0)
        {
            if (errno != EINTR)
            {
                return PAL_ERR_UNKNOWN;
            }
            requested = remaining;
        }

        duration_ns -= seconds * UINT64_C(1000000000) + nanoseconds;
    }

    return PAL_OK;
#endif
}
