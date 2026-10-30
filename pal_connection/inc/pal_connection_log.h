/**
 * @file pal_connection_log.h
 * @brief Internal SQLite logging interface for the connection layer.
 *
 * This header is private to pal_connection and is not part of its public API.
 * The logger creates or reuses logs.db in the process working directory.
 */

#ifndef PAL_CONNECTION_LOG_H
#define PAL_CONNECTION_LOG_H

#include <stddef.h>
#include <stdint.h>

/* Opens logs.db, creates the connection_logs table if needed, and prepares
 * the shared insert statement. Call before starting connection workers.
 * Returns PAL_OK on success or a PAL error if logger initialization fails.
 * Logging failures are reported to stderr and do not stop connection work. */
int32_t pal_connection_log_initialize(void);

/* Appends an operation record to the shared SQLite database.
 * operation names the operation; direction is "in", "out", or NULL.
 * transport and role identify the connection; peer_id is zero when unused.
 * status records the PAL result. payload and payload_size describe optional
 * raw bytes stored as a BLOB. A NULL payload is valid when size is zero.
 * This function serializes concurrent callers and reports write failures to
 * stderr without changing connection behavior. */
void pal_connection_log_write(
    const char *operation,
    const char *direction,
    int32_t transport,
    const char *role,
    uint64_t peer_id,
    int32_t status,
    const void *payload,
    size_t payload_size);

#endif
