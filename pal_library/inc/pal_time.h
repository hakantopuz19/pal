/**
 * @file pal_time.h
 * @brief Monotonic time, UTC time, sleep, stopwatch, and deadline APIs.
 */

#ifndef PAL_TIME_H
#define PAL_TIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_NANOSECONDS_PER_MICROSECOND UINT64_C(1000)
#define PAL_NANOSECONDS_PER_MILLISECOND UINT64_C(1000000)
#define PAL_NANOSECONDS_PER_SECOND UINT64_C(1000000000)

typedef struct pal_stopwatch
{
    uint64_t started_at_ns;
    uint64_t stopped_at_ns;
    bool started;
    bool stopped;
} pal_stopwatch_t;

typedef struct pal_deadline
{
    uint64_t expires_at_ns;
} pal_deadline_t;

/* Returns monotonic time since an unspecified origin in nanoseconds.
 * The value is only meaningful for measuring elapsed durations. */
int32_t pal_time_monotonic_ns(uint64_t *time_ns);
int32_t pal_time_monotonic_us(uint64_t *time_us);
int32_t pal_time_monotonic_ms(uint64_t *time_ms);

/* Returns nanoseconds since 1970-01-01 00:00:00 UTC. */
int32_t pal_time_utc_ns(uint64_t *time_ns);

/* Sleeps for the requested duration; interruption is handled internally. */
int32_t pal_time_sleep_ns(uint64_t duration_ns);
int32_t pal_time_sleep_us(uint64_t duration_us);
int32_t pal_time_sleep_ms(uint64_t duration_ms);

/* Starts/restarts a stopwatch, stops it, and retrieves elapsed nanoseconds.
 * Elapsed time can be queried while running or after it is stopped. */
int32_t pal_stopwatch_start(pal_stopwatch_t *stopwatch);
int32_t pal_stopwatch_stop(pal_stopwatch_t *stopwatch);
int32_t pal_stopwatch_elapsed_ns(
    const pal_stopwatch_t *stopwatch,
    uint64_t *elapsed_ns);
int32_t pal_stopwatch_elapsed_us(
    const pal_stopwatch_t *stopwatch,
    uint64_t *elapsed_us);
int32_t pal_stopwatch_elapsed_ms(
    const pal_stopwatch_t *stopwatch,
    uint64_t *elapsed_ms);

/* Creates a deadline from a relative nanosecond duration and reports the
 * remaining time. Remaining time is zero after expiration. */
int32_t pal_deadline_init(pal_deadline_t *deadline, uint64_t duration_ns);
int32_t pal_deadline_remaining_ns(
    const pal_deadline_t *deadline,
    uint64_t *remaining_ns);
int32_t pal_deadline_is_expired(
    const pal_deadline_t *deadline,
    bool *is_expired);

#ifdef __cplusplus
}
#endif

#endif
