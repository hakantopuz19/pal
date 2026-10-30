/**
 * @file pal_connection.h
 * @brief Autonomous TCP and UDP client/server connection services.
 */

#ifndef PAL_CONNECTION_H
#define PAL_CONNECTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pal_connection pal_connection_t;
struct pal_socket_address;

typedef enum pal_connection_transport
{
    PAL_CONNECTION_TRANSPORT_TCP = 1,
    PAL_CONNECTION_TRANSPORT_UDP = 2
} pal_connection_transport_t;

typedef struct pal_connection_event
{
    uint64_t peer_id;
    /* Callback data is valid only during the callback; queued events own it
     * until pal_connection_event_release() is called. */
    const void *data;
    size_t size;
} pal_connection_event_t;

/* Runs on a receive worker. TCP server callbacks for different peers may run
 * concurrently. Return promptly and do not close this connection here. */
typedef void (*pal_connection_event_callback_t)(
    pal_connection_t *connection,
    const pal_connection_event_t *event,
    void *user_data);

/* Opens and starts a TCP listener or bound UDP server. Received data is
 * delivered to the optional callback and to the event queue. A zero port
 * requests an OS-assigned port. Opening a service initializes the SQLite
 * operation log in the process working directory as logs.db. */
int32_t pal_connection_open_server(
    pal_connection_transport_t transport,
    const struct pal_socket_address *local_address,
    pal_connection_event_callback_t callback,
    void *user_data,
    pal_connection_t **connection);

/* Opens and starts a TCP or UDP client. TCP connect waits without a timeout;
 * UDP sends use the supplied remote endpoint. All connection operations and
 * received/sent payloads are persisted in logs.db as SQLite records. */
int32_t pal_connection_open_client(
    pal_connection_transport_t transport,
    const struct pal_socket_address *remote_address,
    pal_connection_event_callback_t callback,
    void *user_data,
    pal_connection_t **connection);

/* Sends data on a TCP client with peer_id zero, or to the specified TCP server
 * peer. UDP clients use their configured peer; UDP servers reply to the most
 * recently received datagram's sender. */
int32_t pal_connection_send(
    pal_connection_t *connection,
    uint64_t peer_id,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms);

/* Retrieves the next queued receive event. The returned event belongs to the
 * caller and must be released with pal_connection_event_release(). */
int32_t pal_connection_next_event(
    pal_connection_t *connection,
    uint32_t timeout_ms,
    pal_connection_event_t **event);

/* Releases an event returned by pal_connection_next_event(). */
void pal_connection_event_release(pal_connection_event_t *event);

/* Reports the number of receive events discarded because the bounded queue
 * was full. */
int32_t pal_connection_get_dropped_event_count(
    pal_connection_t *connection,
    uint64_t *count);

/* Retrieves the first unexpected error reported by a background worker.
 * PAL_OK means no worker failure has been recorded. */
int32_t pal_connection_get_worker_status(
    pal_connection_t *connection,
    int32_t *worker_status);

/* Retrieves the local address, including an OS-assigned port. */
int32_t pal_connection_get_local_address(
    pal_connection_t *connection,
    struct pal_socket_address *address);

/* Stops background workers, closes sockets, and releases the connection.
 * All still-queued events are discarded. Services left open are also closed
 * on normal process exit (main return or exit), but not after abnormal exit. */
int32_t pal_connection_close(pal_connection_t *connection);

#ifdef __cplusplus
}
#endif

#endif
