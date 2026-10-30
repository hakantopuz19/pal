/**
 * @file pal_time.c
 * @brief Platform-independent time, stopwatch, and deadline operations.
 */

#include <stddef.h>
#include <stdint.h>

#include "pal_error.h"
#include "pal_time.h"
#include "pal_backend.h"

/**
 * @brief      Reads monotonic time in nanoseconds.
 *
 * @details    Delegates to the platform clock whose origin is unspecified and
 *             which does not move backwards with wall-clock adjustments.
 *
 * @param[out] time_ns  Receives the monotonic timestamp.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Timestamp retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        Platform clock query failed.
 *
 * @note       Use differences between readings to measure durations.
 * @warning    Do not interpret the value as calendar time.
 */
int32_t pal_time_monotonic_ns(uint64_t *time_ns)
{
    return pal_time_backend_monotonic_ns(time_ns);
}

/**
 * @brief      Reads monotonic time in microseconds.
 *
 * @details    Reads the monotonic nanosecond clock and truncates sub-
 *             microsecond precision.
 *
 * @param[out] time_us  Receives the monotonic timestamp in microseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Timestamp retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        Platform clock query failed.
 *
 * @note       The clock origin is unspecified.
 * @warning    Use nanoseconds when sub-microsecond precision is required.
 */
int32_t pal_time_monotonic_us(uint64_t *time_us)
{
    uint64_t time_ns;
    int32_t status;

    if (time_us == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_time_monotonic_ns(&time_ns);
    if (status == PAL_OK)
    {
        *time_us = time_ns / PAL_NANOSECONDS_PER_MICROSECOND;
    }
    return status;
}

/**
 * @brief      Reads monotonic time in milliseconds.
 *
 * @details    Reads the monotonic nanosecond clock and truncates sub-
 *             millisecond precision.
 *
 * @param[out] time_ms  Receives the monotonic timestamp in milliseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Timestamp retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        Platform clock query failed.
 *
 * @note       The clock origin is unspecified.
 * @warning    Use nanoseconds when sub-millisecond precision is required.
 */
int32_t pal_time_monotonic_ms(uint64_t *time_ms)
{
    uint64_t time_ns;
    int32_t status;

    if (time_ms == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_time_monotonic_ns(&time_ns);
    if (status == PAL_OK)
    {
        *time_ms = time_ns / PAL_NANOSECONDS_PER_MILLISECOND;
    }
    return status;
}

/**
 * @brief      Reads current UTC time as nanoseconds since the Unix epoch.
 *
 * @details    Delegates to the platform wall clock and returns a timestamp
 *             relative to 1970-01-01 00:00:00 UTC.
 *
 * @param[out] time_ns  Receives the UTC Unix timestamp in nanoseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Timestamp retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        Platform clock query failed.
 * @retval     PAL_ERR_OUT_OF_RANGE   Platform time cannot fit the result.
 *
 * @note       Wall-clock time may move due to clock synchronization.
 * @warning    Use the monotonic clock for measuring elapsed time.
 */
int32_t pal_time_utc_ns(uint64_t *time_ns)
{
    return pal_time_backend_utc_ns(time_ns);
}

/**
 * @brief      Sleeps for a duration expressed in nanoseconds.
 *
 * @details    Delegates to a relative platform sleep and resumes the wait
 *             after interrupt signals where supported.
 *
 * @param[in]  duration_ns  Requested sleep duration in nanoseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Requested sleep completed.
 * @retval     PAL_ERR_OUT_OF_RANGE   Duration exceeds the platform timer range.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED Timer resource could not be created.
 * @retval     PAL_ERR_UNKNOWN        Platform sleep operation failed.
 *
 * @note       Actual sleep duration may exceed the requested duration due to
 *             scheduler and timer resolution.
 * @warning    Sleep duration is not a real-time scheduling guarantee.
 */
int32_t pal_time_sleep_ns(uint64_t duration_ns)
{
    return pal_time_backend_sleep_ns(duration_ns);
}

/**
 * @brief      Sleeps for a duration expressed in microseconds.
 *
 * @details    Converts the duration to nanoseconds and delegates to the
 *             platform sleep function.
 *
 * @param[in]  duration_us  Requested sleep duration in microseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Requested sleep completed.
 * @retval     PAL_ERR_OVERFLOW       Conversion to nanoseconds overflows.
 * @retval     PAL_ERR_OUT_OF_RANGE   Duration exceeds the platform timer range.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED Timer resource could not be created.
 * @retval     PAL_ERR_UNKNOWN        Platform sleep operation failed.
 *
 * @note       Actual sleep duration may exceed the requested duration.
 * @warning    Sleep duration is not a real-time scheduling guarantee.
 */
int32_t pal_time_sleep_us(uint64_t duration_us)
{
    if (duration_us > UINT64_MAX / PAL_NANOSECONDS_PER_MICROSECOND)
    {
        return PAL_ERR_OVERFLOW;
    }
    return pal_time_sleep_ns(duration_us * PAL_NANOSECONDS_PER_MICROSECOND);
}

/**
 * @brief      Sleeps for a duration expressed in milliseconds.
 *
 * @details    Converts the duration to nanoseconds and delegates to the
 *             platform sleep function.
 *
 * @param[in]  duration_ms  Requested sleep duration in milliseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Requested sleep completed.
 * @retval     PAL_ERR_OVERFLOW       Conversion to nanoseconds overflows.
 * @retval     PAL_ERR_OUT_OF_RANGE   Duration exceeds the platform timer range.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED Timer resource could not be created.
 * @retval     PAL_ERR_UNKNOWN        Platform sleep operation failed.
 *
 * @note       Actual sleep duration may exceed the requested duration.
 * @warning    Sleep duration is not a real-time scheduling guarantee.
 */
int32_t pal_time_sleep_ms(uint64_t duration_ms)
{
    if (duration_ms > UINT64_MAX / PAL_NANOSECONDS_PER_MILLISECOND)
    {
        return PAL_ERR_OVERFLOW;
    }
    return pal_time_sleep_ns(duration_ms * PAL_NANOSECONDS_PER_MILLISECOND);
}

/**
 * @brief      Starts or restarts a stopwatch.
 *
 * @details    Reads the monotonic clock and resets the stopwatch state to a
 *             running measurement.
 *
 * @param[out] stopwatch  Stopwatch state to initialize.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Stopwatch started.
 * @retval     PAL_ERR_NULL_POINTER   Stopwatch pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        Monotonic clock query failed.
 *
 * @note       Starting an already running or stopped stopwatch restarts it.
 * @warning    The stopwatch object must remain valid while in use.
 */
int32_t pal_stopwatch_start(pal_stopwatch_t *stopwatch)
{
    uint64_t now;
    int32_t status;

    if (stopwatch == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_time_monotonic_ns(&now);
    if (status != PAL_OK)
    {
        return status;
    }

    stopwatch->started_at_ns = now;
    stopwatch->stopped_at_ns = 0u;
    stopwatch->started = true;
    stopwatch->stopped = false;
    return PAL_OK;
}

/**
 * @brief      Stops a running stopwatch.
 *
 * @details    Records the current monotonic time as the end of the measurement.
 *
 * @param[in,out] stopwatch  Stopwatch to stop.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Stopwatch stopped.
 * @retval     PAL_ERR_NULL_POINTER   Stopwatch pointer is NULL.
 * @retval     PAL_ERR_INVALID_STATE  Stopwatch has not been started.
 * @retval     PAL_ERR_UNKNOWN        Monotonic clock query failed.
 *
 * @note       Stopping an already stopped stopwatch succeeds without changing it.
 * @warning    Do not modify stopwatch fields while an operation is using them.
 */
int32_t pal_stopwatch_stop(pal_stopwatch_t *stopwatch)
{
    uint64_t now;
    int32_t status;

    if (stopwatch == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (!stopwatch->started)
    {
        return PAL_ERR_INVALID_STATE;
    }
    if (stopwatch->stopped)
    {
        return PAL_OK;
    }

    status = pal_time_monotonic_ns(&now);
    if (status != PAL_OK)
    {
        return status;
    }

    stopwatch->stopped_at_ns = now;
    stopwatch->stopped = true;
    return PAL_OK;
}

/**
 * @brief      Gets elapsed stopwatch time in nanoseconds.
 *
 * @details    Returns the difference between the start time and either the
 *             current monotonic time or the recorded stop time.
 *
 * @param[in]  stopwatch  Stopwatch to query.
 * @param[out] elapsed_ns Receives elapsed nanoseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Elapsed time retrieved.
 * @retval     PAL_ERR_NULL_POINTER   An input or output pointer is NULL.
 * @retval     PAL_ERR_INVALID_STATE  Stopwatch has not been started.
 * @retval     PAL_ERR_UNKNOWN        Monotonic clock query failed.
 *
 * @note       Querying a running stopwatch does not stop it.
 * @warning    The stopwatch must not be concurrently modified.
 */
int32_t pal_stopwatch_elapsed_ns(
    const pal_stopwatch_t *stopwatch,
    uint64_t *elapsed_ns)
{
    uint64_t end_time;
    int32_t status;

    if (stopwatch == NULL || elapsed_ns == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (!stopwatch->started)
    {
        return PAL_ERR_INVALID_STATE;
    }

    if (stopwatch->stopped)
    {
        end_time = stopwatch->stopped_at_ns;
    }
    else
    {
        status = pal_time_monotonic_ns(&end_time);
        if (status != PAL_OK)
        {
            return status;
        }
    }

    *elapsed_ns = end_time - stopwatch->started_at_ns;
    return PAL_OK;
}

/**
 * @brief      Gets elapsed stopwatch time in microseconds.
 *
 * @details    Reads elapsed nanoseconds and truncates sub-microsecond precision.
 *
 * @param[in]  stopwatch  Stopwatch to query.
 * @param[out] elapsed_us Receives elapsed microseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Elapsed time retrieved.
 * @retval     PAL_ERR_NULL_POINTER   An input or output pointer is NULL.
 * @retval     PAL_ERR_INVALID_STATE  Stopwatch has not been started.
 * @retval     PAL_ERR_UNKNOWN        Monotonic clock query failed.
 *
 * @note       Querying a running stopwatch does not stop it.
 * @warning    Sub-microsecond precision is discarded.
 */
int32_t pal_stopwatch_elapsed_us(
    const pal_stopwatch_t *stopwatch,
    uint64_t *elapsed_us)
{
    uint64_t elapsed_ns;
    int32_t status;

    if (elapsed_us == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    status = pal_stopwatch_elapsed_ns(stopwatch, &elapsed_ns);
    if (status == PAL_OK)
    {
        *elapsed_us = elapsed_ns / PAL_NANOSECONDS_PER_MICROSECOND;
    }
    return status;
}

/**
 * @brief      Gets elapsed stopwatch time in milliseconds.
 *
 * @details    Reads elapsed nanoseconds and truncates sub-millisecond precision.
 *
 * @param[in]  stopwatch  Stopwatch to query.
 * @param[out] elapsed_ms Receives elapsed milliseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Elapsed time retrieved.
 * @retval     PAL_ERR_NULL_POINTER   An input or output pointer is NULL.
 * @retval     PAL_ERR_INVALID_STATE  Stopwatch has not been started.
 * @retval     PAL_ERR_UNKNOWN        Monotonic clock query failed.
 *
 * @note       Querying a running stopwatch does not stop it.
 * @warning    Sub-millisecond precision is discarded.
 */
int32_t pal_stopwatch_elapsed_ms(
    const pal_stopwatch_t *stopwatch,
    uint64_t *elapsed_ms)
{
    uint64_t elapsed_ns;
    int32_t status;

    if (elapsed_ms == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    status = pal_stopwatch_elapsed_ns(stopwatch, &elapsed_ns);
    if (status == PAL_OK)
    {
        *elapsed_ms = elapsed_ns / PAL_NANOSECONDS_PER_MILLISECOND;
    }
    return status;
}

/**
 * @brief      Initializes a deadline using a relative duration.
 *
 * @details    Adds the requested duration to the current monotonic time and
 *             stores the resulting absolute monotonic expiration.
 *
 * @param[out] deadline    Deadline object to initialize.
 * @param[in]  duration_ns Duration from now until expiration.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Deadline initialized.
 * @retval     PAL_ERR_NULL_POINTER   Deadline pointer is NULL.
 * @retval     PAL_ERR_OVERFLOW       Absolute expiration would overflow.
 * @retval     PAL_ERR_UNKNOWN        Monotonic clock query failed.
 *
 * @note       A zero duration creates an already expired deadline.
 * @warning    Do not compare deadlines created from different clock sources.
 */
int32_t pal_deadline_init(pal_deadline_t *deadline, uint64_t duration_ns)
{
    uint64_t now;
    int32_t status;

    if (deadline == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_time_monotonic_ns(&now);
    if (status != PAL_OK)
    {
        return status;
    }
    if (duration_ns > UINT64_MAX - now)
    {
        return PAL_ERR_OVERFLOW;
    }

    deadline->expires_at_ns = now + duration_ns;
    return PAL_OK;
}

/**
 * @brief      Gets the remaining time before a deadline in nanoseconds.
 *
 * @details    Compares the deadline with the current monotonic clock and
 *             returns zero when it has expired.
 *
 * @param[in]  deadline     Deadline to query.
 * @param[out] remaining_ns Receives remaining nanoseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Remaining time retrieved.
 * @retval     PAL_ERR_NULL_POINTER   An input or output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        Monotonic clock query failed.
 *
 * @note       A zero remaining value means the deadline has expired.
 * @warning    The value can become stale immediately after it is read.
 */
int32_t pal_deadline_remaining_ns(
    const pal_deadline_t *deadline,
    uint64_t *remaining_ns)
{
    uint64_t now;
    int32_t status;

    if (deadline == NULL || remaining_ns == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_time_monotonic_ns(&now);
    if (status != PAL_OK)
    {
        return status;
    }

    *remaining_ns = (now >= deadline->expires_at_ns)
        ? 0u
        : deadline->expires_at_ns - now;
    return PAL_OK;
}

/**
 * @brief      Reports whether a deadline has expired.
 *
 * @details    Reads the remaining duration and sets the output flag when no
 *             time remains.
 *
 * @param[in]  deadline    Deadline to query.
 * @param[out] is_expired  Receives the expiration state.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Expiration state retrieved.
 * @retval     PAL_ERR_NULL_POINTER   An input or output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        Monotonic clock query failed.
 *
 * @note       A zero-duration deadline is considered expired.
 * @warning    Expiration can occur immediately after a false result.
 */
int32_t pal_deadline_is_expired(
    const pal_deadline_t *deadline,
    bool *is_expired)
{
    uint64_t remaining_ns;
    int32_t status;

    if (is_expired == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_deadline_remaining_ns(deadline, &remaining_ns);
    if (status == PAL_OK)
    {
        *is_expired = (remaining_ns == 0u);
    }
    return status;
}
