/**
 * @file pal_connection_log.c
 * @brief SQLite-backed operation and payload logging for pal_connection.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <sqlite3.h>

#include "pal_connection.h"
#include "pal_connection_log.h"
#include "pal_error.h"
#include "pal_mutex.h"

#if SQLITE_VERSION_NUMBER < 3008007
#error "pal_connection requires SQLite 3.8.7 or newer."
#endif

#define PAL_CONNECTION_LOG_DATABASE "logs.db"

static sqlite3 *g_log_database;
static sqlite3_stmt *g_log_statement;
static bool g_log_shutdown_registered;
static bool g_log_initialization_attempted;

/**
 * @brief      Acquires the logger lock.
 *
 * @details    Serializes SQLite access across connection workers and callers.
 *
 * @return     PAL_OK when acquired or PAL_ERR_UNKNOWN on POSIX lock failure.
 *
 * @note       The Windows SRW lock is statically initialized.
 * @warning    Pair every successful call with pal_connection_log_unlock().
 */
static int32_t pal_connection_log_lock(void)
{
    return pal_mutex_global_lock();
}

/**
 * @brief      Releases the logger lock.
 *
 * @details    Releases the native lock protecting the SQLite connection.
 *
 * @return     This function does not return a value.
 *
 * @note       POSIX unlock errors are treated as internal synchronization errors.
 * @warning    The caller must own the logger lock.
 */
static void pal_connection_log_unlock(void)
{
    (void)pal_mutex_global_unlock();
}

/**
 * @brief      Finalizes and closes the process-wide SQLite logger.
 *
 * @details    Runs during normal process exit after connection cleanup has
 *             stopped workers and recorded their close operations.
 *
 * @return     This function does not return a value.
 *
 * @note       Cleanup errors are reported to stderr.
 * @warning    This handler is not called after abnormal process termination.
 */
static void pal_connection_log_shutdown(void)
{
    int sqlite_status;

    if (pal_connection_log_lock() != PAL_OK)
    {
        (void)fprintf(stderr, "pal_connection: cannot lock SQLite logger at exit\n");
        return;
    }

    if (g_log_statement != NULL)
    {
        sqlite_status = sqlite3_finalize(g_log_statement);
        g_log_statement = NULL;
        if (sqlite_status != SQLITE_OK)
        {
            (void)fprintf(
                stderr,
                "pal_connection: SQLite statement cleanup failed: %s\n",
                sqlite3_errstr(sqlite_status));
        }
    }
    if (g_log_database != NULL)
    {
        sqlite_status = sqlite3_close(g_log_database);
        if (sqlite_status != SQLITE_OK)
        {
            (void)fprintf(
                stderr,
                "pal_connection: SQLite database close failed: %s\n",
                sqlite3_errmsg(g_log_database));
        }
        else
        {
            g_log_database = NULL;
        }
    }
    pal_connection_log_unlock();
}

/**
 * @brief      Opens logs.db and prepares its logging statement.
 *
 * @details    Creates the connection_logs table if needed and installs a
 *             process-exit handler for closing the database.
 *
 * @return     PAL_OK on success or PAL_ERR_IO when SQLite initialization fails.
 *
 * @note       The caller must hold the logger lock.
 * @warning    The database is opened relative to the process working directory.
 */
static int32_t pal_connection_log_initialize_locked(void)
{
    static const char create_table_sql[] =
        "CREATE TABLE IF NOT EXISTS connection_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "operation TEXT NOT NULL,"
        "direction TEXT,"
        "transport TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "peer_id TEXT NOT NULL,"
        "status INTEGER NOT NULL,"
        "payload_size INTEGER NOT NULL,"
        "payload BLOB"
        ")";
    static const char insert_sql[] =
        "INSERT INTO connection_logs ("
        "operation, direction, transport, role, peer_id, status, "
        "payload_size, payload"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    int sqlite_status;

    if (g_log_database != NULL && g_log_statement != NULL)
    {
        return PAL_OK;
    }
    if (g_log_initialization_attempted)
    {
        return PAL_ERR_IO;
    }
    g_log_initialization_attempted = true;

    sqlite_status = sqlite3_open_v2(
        PAL_CONNECTION_LOG_DATABASE,
        &g_log_database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        NULL);
    if (sqlite_status != SQLITE_OK)
    {
        (void)fprintf(
            stderr,
            "pal_connection: cannot open %s: %s\n",
            PAL_CONNECTION_LOG_DATABASE,
            (g_log_database != NULL)
                ? sqlite3_errmsg(g_log_database)
                : sqlite3_errstr(sqlite_status));
        if (g_log_database != NULL)
        {
            (void)sqlite3_close_v2(g_log_database);
            g_log_database = NULL;
        }
        return PAL_ERR_IO;
    }

    sqlite_status = sqlite3_busy_timeout(g_log_database, 5000);
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_exec(
            g_log_database,
            create_table_sql,
            NULL,
            NULL,
            NULL);
    }
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_prepare_v2(
            g_log_database,
            insert_sql,
            -1,
            &g_log_statement,
            NULL);
    }
    if (sqlite_status != SQLITE_OK)
    {
        (void)fprintf(
            stderr,
            "pal_connection: cannot initialize %s: %s\n",
            PAL_CONNECTION_LOG_DATABASE,
            sqlite3_errmsg(g_log_database));
        if (g_log_statement != NULL)
        {
            (void)sqlite3_finalize(g_log_statement);
            g_log_statement = NULL;
        }
        (void)sqlite3_close_v2(g_log_database);
        g_log_database = NULL;
        return PAL_ERR_IO;
    }

    if (!g_log_shutdown_registered)
    {
        if (atexit(pal_connection_log_shutdown) != 0)
        {
            (void)fprintf(
                stderr,
                "pal_connection: cannot register SQLite logger shutdown\n");
        }
        else
        {
            g_log_shutdown_registered = true;
        }
    }
    return PAL_OK;
}

/**
 * @brief      Initializes the connection log database.
 *
 * @details    Acquires the logger lock and opens or reuses logs.db.
 *
 * @return     PAL_OK on success or a negative PAL error status.
 *
 * @note       Logging failure does not prevent connection operations.
 * @warning    Initialization errors are reported to stderr.
 */
int32_t pal_connection_log_initialize(void)
{
    int32_t status = pal_connection_log_lock();

    if (status != PAL_OK)
    {
        (void)fprintf(stderr, "pal_connection: cannot lock SQLite logger\n");
        return status;
    }
    status = pal_connection_log_initialize_locked();
    pal_connection_log_unlock();
    return status;
}

/**
 * @brief      Returns the text name of a connection transport.
 *
 * @details    Converts the public transport enum to the stable database value.
 *
 * @param[in]  transport  Transport to describe.
 *
 * @return     "tcp", "udp", or "unknown".
 *
 * @note       The returned string has static storage duration.
 * @warning    The returned pointer must not be freed.
 */
static const char *pal_connection_log_transport_name(
    int32_t transport)
{
    if (transport == PAL_CONNECTION_TRANSPORT_TCP)
    {
        return "tcp";
    }
    if (transport == PAL_CONNECTION_TRANSPORT_UDP)
    {
        return "udp";
    }
    return "unknown";
}

/**
 * @brief      Appends a connection operation and optional raw payload to SQLite.
 *
 * @details    Inserts metadata and payload bytes as a BLOB using a serialized,
 *             prepared statement. On database failure, reports the error to
 *             stderr and returns without changing connection behavior.
 *
 * @param[in]  operation    Operation name.
 * @param[in]  direction    "in", "out", or NULL when not applicable.
 * @param[in]  transport    TCP or UDP transport.
 * @param[in]  role         Client/server role name.
 * @param[in]  peer_id      TCP peer identifier, or zero when not applicable.
 * @param[in]  status       PAL result status for the operation.
 * @param[in]  payload      Optional bytes to persist as a BLOB.
 * @param[in]  payload_size Number of payload bytes.
 *
 * @return     This function does not return a value.
 *
 * @note       Payload data is copied into SQLite before this call returns.
 * @warning    The payload is stored unencrypted and may contain sensitive data.
 */
void pal_connection_log_write(
    const char *operation,
    const char *direction,
    int32_t transport,
    const char *role,
    uint64_t peer_id,
    int32_t status,
    const void *payload,
    size_t payload_size)
{
    char peer_id_text[32];
    int sqlite_status;

    if (pal_connection_log_lock() != PAL_OK)
    {
        (void)fprintf(stderr, "pal_connection: cannot lock SQLite logger\n");
        return;
    }
    if (pal_connection_log_initialize_locked() != PAL_OK)
    {
        pal_connection_log_unlock();
        return;
    }

    (void)snprintf(
        peer_id_text,
        sizeof(peer_id_text),
        "%" PRIu64,
        peer_id);
    (void)sqlite3_reset(g_log_statement);
    sqlite_status = sqlite3_clear_bindings(g_log_statement);
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_bind_text(
            g_log_statement, 1, operation, -1, SQLITE_STATIC);
    }
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = (direction == NULL)
            ? sqlite3_bind_null(g_log_statement, 2)
            : sqlite3_bind_text(g_log_statement, 2, direction, -1, SQLITE_STATIC);
    }
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_bind_text(
            g_log_statement,
            3,
            pal_connection_log_transport_name(transport),
            -1,
            SQLITE_STATIC);
    }
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_bind_text(
            g_log_statement, 4, role, -1, SQLITE_STATIC);
    }
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_bind_text(
            g_log_statement, 5, peer_id_text, -1, SQLITE_TRANSIENT);
    }
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_bind_int(g_log_statement, 6, status);
    }
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_bind_int64(
            g_log_statement,
            7,
            (payload_size > (size_t)INT64_MAX)
                ? INT64_MAX
                : (sqlite3_int64)payload_size);
    }
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_bind_blob64(
            g_log_statement,
            8,
            payload,
            (sqlite3_uint64)payload_size,
            SQLITE_TRANSIENT);
    }
    if (sqlite_status == SQLITE_OK)
    {
        sqlite_status = sqlite3_step(g_log_statement);
        if (sqlite_status == SQLITE_DONE)
        {
            sqlite_status = SQLITE_OK;
        }
    }
    if (sqlite_status != SQLITE_OK)
    {
        (void)fprintf(
            stderr,
            "pal_connection: failed to write %s log to %s: %s\n",
            operation,
            PAL_CONNECTION_LOG_DATABASE,
            sqlite3_errmsg(g_log_database));
    }
    pal_connection_log_unlock();
}
