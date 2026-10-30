/**
 * @file pal_connection.c
 * @brief Autonomous connection services built on the PAL socket and thread APIs.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pal_connection.h"
#include "pal_connection_log.h"
#include "pal_error.h"
#include "pal_mutex.h"
#include "pal_queue.h"
#include "pal_socket.h"
#include "pal_thread.h"
#include "pal_types.h"

#define PAL_CONNECTION_EVENT_QUEUE_CAPACITY 64u
#define PAL_CONNECTION_POLL_INTERVAL_MS 100u
#define PAL_CONNECTION_RECEIVE_BUFFER_SIZE 4096u
#define PAL_CONNECTION_DEFAULT_BACKLOG 8

typedef enum pal_connection_role
{
    PAL_CONNECTION_ROLE_TCP_LISTENER,
    PAL_CONNECTION_ROLE_TCP_CLIENT,
    PAL_CONNECTION_ROLE_TCP_PEER,
    PAL_CONNECTION_ROLE_UDP_SERVER,
    PAL_CONNECTION_ROLE_UDP_CLIENT
} pal_connection_role_t;

struct pal_connection
{
    pal_socket_t *socket;
    pal_connection_transport_t transport;
    pal_connection_role_t role;
    pal_connection_event_callback_t callback;
    void *user_data;
    pal_queue_t *events;
    pal_mutex_t *mutex;
    pal_thread_t *worker;
    struct pal_connection *registry_next;
    struct pal_connection *peer_next;
    struct pal_connection *parent;
    pal_socket_address_t remote_address;
    pal_socket_address_t last_peer_address;
    uint64_t peer_id;
    uint64_t next_peer_id;
    uint64_t dropped_events;
    int32_t worker_status;
    bool stopping;
    bool has_last_peer;
    bool worker_finished;
    bool registered;
};

static pal_connection_t *g_pal_connection_registry;
static bool g_pal_connection_exit_handler_registered;

static void pal_connection_cleanup_at_exit(void);
static int32_t pal_connection_close_internal(
    pal_connection_t *connection,
    bool *released);

static const char *pal_connection_role_name(pal_connection_role_t role)
{
    switch (role)
    {
        case PAL_CONNECTION_ROLE_TCP_LISTENER:
            return "tcp_server";
        case PAL_CONNECTION_ROLE_TCP_CLIENT:
            return "tcp_client";
        case PAL_CONNECTION_ROLE_TCP_PEER:
            return "tcp_peer";
        case PAL_CONNECTION_ROLE_UDP_SERVER:
            return "udp_server";
        case PAL_CONNECTION_ROLE_UDP_CLIENT:
            return "udp_client";
        default:
            return "unknown";
    }
}

static void pal_connection_log_service(
    const char *operation,
    const char *direction,
    const pal_connection_t *connection,
    uint64_t peer_id,
    int32_t status,
    const void *payload,
    size_t payload_size)
{
    pal_connection_log_write(
        operation,
        direction,
        connection->transport,
        pal_connection_role_name(connection->role),
        peer_id,
        status,
        payload,
        payload_size);
}

/**
 * @brief      Locks the process-wide connection registry.
 *
 * @details    Uses the process-wide mutex provided by the PAL synchronization
 *             API so the registry remains safe before service creation.
 *
 * @param[in]  None.
 *
 * @return     PAL_OK when the registry lock was acquired.
 *
 * @note       The lock is held only while changing registry links.
 * @warning    Never perform socket I/O or join a worker while holding it.
 */
static int32_t pal_connection_registry_lock(void)
{
    return pal_mutex_global_lock();
}

/**
 * @brief      Unlocks the process-wide connection registry.
 *
 * @details    Releases the PAL process-wide mutex used to protect the
 *             global connection list.
 *
 * @param[in]  None.
 *
 * @return     This function does not return a value.
 *
 * @note       POSIX unlock errors indicate an internal programming failure.
 * @warning    The caller must own the registry lock.
 */
static void pal_connection_registry_unlock(void)
{
    (void)pal_mutex_global_unlock();
}

/**
 * @brief      Reads whether a connection service has been asked to stop.
 *
 * @details    Checks the owning service's stopping state under its mutex. TCP
 *             peer workers use their listener's state so one close request
 *             stops the listener and all of its peer workers.
 *
 * @param[in]  connection  Service or accepted TCP peer to inspect.
 *
 * @return     true when the service is stopping.
 *
 * @note       The check is intentionally brief and never held during I/O.
 * @warning    The connection object must remain alive during the call.
 */
static bool pal_connection_is_stopping(pal_connection_t *connection)
{
    pal_connection_t *owner =
        (connection->role == PAL_CONNECTION_ROLE_TCP_PEER)
            ? connection->parent
            : connection;
    bool stopping;

    if (pal_mutex_lock(owner->mutex) != PAL_OK)
    {
        return true;
    }
    stopping = owner->stopping;
    (void)pal_mutex_unlock(owner->mutex);
    return stopping;
}

/**
 * @brief      Records a background worker failure.
 *
 * @details    Saves the first non-success status on the owning service so
 *             unexpected receive or accept failures remain observable.
 *
 * @param[in]  connection  Service or accepted TCP peer that encountered error.
 * @param[in]  status      PAL failure status to record.
 *
 * @return     This function does not return a value.
 *
 * @note       Timeout and non-blocking statuses are handled by the worker loop.
 * @warning    The supplied status must be a negative PAL status.
 */
static void pal_connection_record_worker_error(
    pal_connection_t *connection,
    int32_t status)
{
    pal_connection_t *owner =
        (connection->role == PAL_CONNECTION_ROLE_TCP_PEER)
            ? connection->parent
            : connection;

    if (pal_mutex_lock(owner->mutex) == PAL_OK)
    {
        if (owner->worker_status == PAL_OK)
        {
            owner->worker_status = status;
        }
        (void)pal_mutex_unlock(owner->mutex);
    }
    pal_connection_log_service(
        "worker_error",
        NULL,
        owner,
        (connection->role == PAL_CONNECTION_ROLE_TCP_PEER)
            ? connection->peer_id
            : 0u,
        status,
        NULL,
        0u);
}

/**
 * @brief      Releases an allocated receive event.
 *
 * @details    Frees the copied data buffer and the event structure. The same
 *             helper is used when queue overflow discards an older event.
 *
 * @param[in]  event  Event allocation to release.
 *
 * @return     This function does not return a value.
 *
 * @note       A NULL event is accepted for cleanup convenience.
 * @warning    The event must not be queued or accessed after this call.
 */
static void pal_connection_event_free(pal_connection_event_t *event)
{
    if (event != NULL)
    {
        free((void *)event->data);
        free(event);
    }
}

/**
 * @brief      Delivers received bytes to the callback and event queue.
 *
 * @details    Copies the payload into an owned event, invokes the optional
 *             callback on the receiving worker, and enqueues the same event
 *             for polling. When the bounded queue is full, the oldest queued
 *             event is discarded to keep workers responsive.
 *
 * @param[in]  connection  Service that owns the callback and event queue.
 * @param[in]  peer_id     Accepted TCP peer identifier, or zero otherwise.
 * @param[in]  data        Received bytes to copy.
 * @param[in]  size        Number of bytes to copy.
 *
 * @return     PAL_OK when the event was queued, otherwise a negative PAL error.
 *
 * @note       The callback's event pointer is valid only for the callback call.
 * @warning    Do not close the connection from its callback.
 */
static int32_t pal_connection_publish_event(
    pal_connection_t *connection,
    uint64_t peer_id,
    const void *data,
    size_t size)
{
    pal_connection_event_t *event;
    void *discarded_event = NULL;
    int32_t status;

    event = (pal_connection_event_t *)calloc(1u, sizeof(*event));
    if (event == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }
    if (size > 0u)
    {
        void *copy = malloc(size);

        if (copy == NULL)
        {
            free(event);
            return PAL_ERR_OUT_OF_MEMORY;
        }
        memcpy(copy, data, size);
        event->data = copy;
    }
    event->peer_id = peer_id;
    event->size = size;

    if (connection->callback != NULL)
    {
        connection->callback(connection, event, connection->user_data);
    }

    status = pal_queue_push(connection->events, event, PAL_NO_WAIT);
    if (status == PAL_ERR_WOULD_BLOCK)
    {
        if (pal_mutex_lock(connection->mutex) != PAL_OK)
        {
            pal_connection_event_free(event);
            return PAL_ERR_UNKNOWN;
        }

        status = pal_queue_pop(connection->events, &discarded_event, PAL_NO_WAIT);
        if (status == PAL_OK)
        {
            pal_connection_event_free((pal_connection_event_t *)discarded_event);
            connection->dropped_events++;
        }
        status = pal_queue_push(connection->events, event, PAL_NO_WAIT);
        if (status != PAL_OK)
        {
            connection->dropped_events++;
        }
        (void)pal_mutex_unlock(connection->mutex);
    }

    if (status != PAL_OK)
    {
        pal_connection_event_free(event);
    }
    return status;
}

/**
 * @brief      Runs a receive loop for a TCP client, TCP peer, or UDP endpoint.
 *
 * @details    Polls the native socket with finite waits so stop requests are
 *             observed promptly. Each received TCP byte chunk or UDP datagram
 *             is published to the service callback and event queue.
 *
 * @param[in]  argument  Connection service or accepted TCP peer.
 *
 * @return     NULL after the worker stops.
 *
 * @note       TCP data is a byte stream; event boundaries are not messages.
 * @warning    User callbacks execute on this worker and must return promptly.
 */
static void *pal_connection_receive_worker(void *argument)
{
    pal_connection_t *worker_connection = (pal_connection_t *)argument;
    pal_connection_t *service =
        (worker_connection->role == PAL_CONNECTION_ROLE_TCP_PEER)
            ? worker_connection->parent
            : worker_connection;
    char buffer[PAL_CONNECTION_RECEIVE_BUFFER_SIZE];

    while (!pal_connection_is_stopping(worker_connection))
    {
        size_t received = 0u;
        uint64_t peer_id = 0u;
        int32_t status;

        if (worker_connection->transport == PAL_CONNECTION_TRANSPORT_UDP)
        {
            pal_socket_address_t source;

            status = pal_socket_receive_from(
                worker_connection->socket,
                buffer,
                sizeof(buffer),
                &received,
                &source,
                PAL_CONNECTION_POLL_INTERVAL_MS);
            if (status == PAL_OK &&
                worker_connection->role == PAL_CONNECTION_ROLE_UDP_SERVER)
            {
                if (pal_mutex_lock(worker_connection->mutex) != PAL_OK)
                {
                    status = PAL_ERR_UNKNOWN;
                }
                else
                {
                    worker_connection->last_peer_address = source;
                    worker_connection->has_last_peer = true;
                    (void)pal_mutex_unlock(worker_connection->mutex);
                }
            }
        }
        else
        {
            status = pal_socket_receive(
                worker_connection->socket,
                buffer,
                sizeof(buffer),
                &received,
                PAL_CONNECTION_POLL_INTERVAL_MS);
            peer_id = worker_connection->peer_id;
            if (status == PAL_OK && received == 0u)
            {
                break;
            }
        }

        if (status == PAL_ERR_TIMEOUT || status == PAL_ERR_WOULD_BLOCK)
        {
            continue;
        }
        if (status != PAL_OK)
        {
            if (!pal_connection_is_stopping(worker_connection))
            {
                pal_connection_record_worker_error(worker_connection, status);
            }
            break;
        }

        pal_connection_log_service(
            "receive",
            "in",
            service,
            peer_id,
            PAL_OK,
            buffer,
            received);
        status = pal_connection_publish_event(service, peer_id, buffer, received);
        if (status != PAL_OK && status != PAL_ERR_WOULD_BLOCK)
        {
            pal_connection_record_worker_error(service, status);
        }
    }

    if (worker_connection->role == PAL_CONNECTION_ROLE_TCP_PEER &&
        pal_mutex_lock(worker_connection->parent->mutex) == PAL_OK)
    {
        worker_connection->worker_finished = true;
        (void)pal_mutex_unlock(worker_connection->parent->mutex);
    }
    return NULL;
}

/**
 * @brief      Stops and releases one accepted TCP peer.
 *
 * @details    Joins its receive worker before closing the socket and freeing
 *             the peer state, ensuring the parent listener remains valid until
 *             the worker has stopped using it.
 *
 * @param[in]  peer  Accepted TCP peer to release.
 *
 * @return     This function does not return a value.
 *
 * @note       The listener worker owns all accepted peer objects.
 * @warning    Do not call this concurrently with peer-list traversal.
 */
static void pal_connection_peer_release(pal_connection_t *peer)
{
    if (peer->worker != NULL)
    {
        (void)pal_thread_join(peer->worker, NULL);
        peer->worker = NULL;
    }
    if (peer->socket != NULL)
    {
        (void)pal_socket_close(peer->socket);
    }
    free(peer);
}

/**
 * @brief      Joins and removes completed TCP peer workers.
 *
 * @details    Detaches finished peers from the listener list while holding its
 *             mutex, then joins and frees them without holding the mutex.
 *
 * @param[in]  server  TCP listener whose peer list is reaped.
 *
 * @return     This function does not return a value.
 *
 * @note       Only the listener worker reaps completed peers.
 * @warning    Never join a peer while holding the listener mutex.
 */
static void pal_connection_reap_peers(pal_connection_t *server)
{
    pal_connection_t *finished = NULL;
    pal_connection_t **cursor;

    if (pal_mutex_lock(server->mutex) != PAL_OK)
    {
        return;
    }
    cursor = &server->peer_next;
    while (*cursor != NULL)
    {
        pal_connection_t *peer = *cursor;

        if (peer->worker_finished)
        {
            *cursor = peer->peer_next;
            peer->peer_next = finished;
            finished = peer;
        }
        else
        {
            cursor = &peer->peer_next;
        }
    }
    (void)pal_mutex_unlock(server->mutex);

    while (finished != NULL)
    {
        pal_connection_t *peer = finished;

        finished = peer->peer_next;
        pal_connection_peer_release(peer);
    }
}

/**
 * @brief      Stops and releases every remaining TCP peer.
 *
 * @details    Removes the complete peer list under the listener mutex and
 *             joins each worker before closing its socket and releasing state.
 *
 * @param[in]  server  TCP listener whose peers are released.
 *
 * @return     This function does not return a value.
 *
 * @note       The listener's stopping flag must already be set.
 * @warning    Call only after the listener accept loop has ended.
 */
static void pal_connection_release_all_peers(pal_connection_t *server)
{
    pal_connection_t *peers;

    if (pal_mutex_lock(server->mutex) != PAL_OK)
    {
        return;
    }
    peers = server->peer_next;
    server->peer_next = NULL;
    (void)pal_mutex_unlock(server->mutex);

    while (peers != NULL)
    {
        pal_connection_t *peer = peers;

        peers = peer->peer_next;
        pal_connection_peer_release(peer);
    }
}

/**
 * @brief      Accepts TCP clients and starts one receive worker per client.
 *
 * @details    Repeatedly accepts with a finite timeout, assigns each peer a
 *             stable identifier, and tracks peer workers until they finish or
 *             the server is closed.
 *
 * @param[in]  argument  TCP listener service.
 *
 * @return     NULL after the server is stopped or its accept operation fails.
 *
 * @note       The server callback receives peer identifiers for TCP replies.
 * @warning    The listener and its worker state must remain alive until join.
 */
static void *pal_connection_tcp_server_worker(void *argument)
{
    pal_connection_t *server = (pal_connection_t *)argument;

    while (!pal_connection_is_stopping(server))
    {
        pal_connection_t *peer;
        int32_t status;

        pal_connection_reap_peers(server);
        peer = (pal_connection_t *)calloc(1u, sizeof(*peer));
        if (peer == NULL)
        {
            pal_connection_record_worker_error(server, PAL_ERR_OUT_OF_MEMORY);
            break;
        }
        peer->transport = PAL_CONNECTION_TRANSPORT_TCP;
        peer->role = PAL_CONNECTION_ROLE_TCP_PEER;
        peer->parent = server;

        status = pal_socket_accept(
            server->socket,
            PAL_CONNECTION_POLL_INTERVAL_MS,
            &peer->socket,
            NULL);
        if (status == PAL_ERR_TIMEOUT || status == PAL_ERR_WOULD_BLOCK)
        {
            free(peer);
            continue;
        }
        if (status != PAL_OK)
        {
            free(peer);
            if (!pal_connection_is_stopping(server))
            {
                pal_connection_record_worker_error(server, status);
            }
            break;
        }

        pal_connection_log_service(
            "accept",
            NULL,
            server,
            0u,
            PAL_OK,
            NULL,
            0u);
        if (pal_mutex_lock(server->mutex) != PAL_OK)
        {
            (void)pal_socket_close(peer->socket);
            free(peer);
            pal_connection_record_worker_error(server, PAL_ERR_UNKNOWN);
            break;
        }
        peer->peer_id = ++server->next_peer_id;
        if (peer->peer_id == 0u)
        {
            peer->peer_id = ++server->next_peer_id;
        }
        peer->peer_next = server->peer_next;
        server->peer_next = peer;
        status = pal_thread_create(
            pal_connection_receive_worker,
            peer,
            &peer->worker);
        if (status != PAL_OK)
        {
            server->peer_next = peer->peer_next;
        }
        (void)pal_mutex_unlock(server->mutex);

        if (status != PAL_OK)
        {
            (void)pal_socket_close(peer->socket);
            free(peer);
            pal_connection_record_worker_error(server, status);
        }
    }

    pal_connection_release_all_peers(server);
    return NULL;
}

/**
 * @brief      Registers an active service for normal process-exit cleanup.
 *
 * @details    Installs a single atexit handler and adds the fully initialized
 *             service to a thread-safe registry.
 *
 * @param[in]  connection  Service to register.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 *
 * @note       Cleanup runs for normal exit() and return from main().
 * @warning    It cannot run after abort(), _Exit(), or abrupt process failure.
 */
static int32_t pal_connection_register(pal_connection_t *connection)
{
    int32_t status = pal_connection_registry_lock();

    if (status != PAL_OK)
    {
        return status;
    }
    if (!g_pal_connection_exit_handler_registered)
    {
        if (atexit(pal_connection_cleanup_at_exit) != 0)
        {
            pal_connection_registry_unlock();
            return PAL_ERR_RESOURCE_EXHAUSTED;
        }
        g_pal_connection_exit_handler_registered = true;
    }

    connection->registry_next = g_pal_connection_registry;
    g_pal_connection_registry = connection;
    connection->registered = true;
    pal_connection_registry_unlock();
    return PAL_OK;
}

/**
 * @brief      Removes a service from the process-exit registry.
 *
 * @details    Unlinks the connection under the registry lock so explicit close
 *             and atexit cleanup cannot both own the same registered object.
 *
 * @param[in]  connection  Service to unregister.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 *
 * @note       A service not present in the registry is left unchanged.
 * @warning    The connection must remain allocated until this call returns.
 */
static int32_t pal_connection_unregister(pal_connection_t *connection)
{
    pal_connection_t **cursor;
    int32_t status = pal_connection_registry_lock();

    if (status != PAL_OK)
    {
        return status;
    }
    cursor = &g_pal_connection_registry;
    while (*cursor != NULL && *cursor != connection)
    {
        cursor = &(*cursor)->registry_next;
    }
    if (*cursor == connection)
    {
        *cursor = connection->registry_next;
        connection->registry_next = NULL;
        connection->registered = false;
    }
    pal_connection_registry_unlock();
    return PAL_OK;
}

/**
 * @brief      Stops workers and releases every registered connection at exit.
 *
 * @details    Removes services one at a time from the global registry and
 *             closes them outside the registry lock so worker joins cannot
 *             block new registry operations.
 *
 * @param[in]  None.
 *
 * @return     This function does not return a value.
 *
 * @note       Registered callbacks must return for process-exit cleanup to
 *             complete.
 * @warning    Do not rely on this handler after abnormal process termination.
 */
static void pal_connection_cleanup_at_exit(void)
{
    for (;;)
    {
        pal_connection_t *connection;
        pal_connection_transport_t transport;
        pal_connection_role_t role;
        bool released = false;
        int32_t status;

        if (pal_connection_registry_lock() != PAL_OK)
        {
            return;
        }
        connection = g_pal_connection_registry;
        if (connection != NULL)
        {
            g_pal_connection_registry = connection->registry_next;
            connection->registry_next = NULL;
            connection->registered = false;
        }
        pal_connection_registry_unlock();

        if (connection == NULL)
        {
            break;
        }
        transport = connection->transport;
        role = connection->role;
        status = pal_connection_close_internal(connection, &released);
        pal_connection_log_write(
            "close",
            NULL,
            transport,
            pal_connection_role_name(role),
            0u,
            status,
            NULL,
            0u);
        if (status != PAL_OK)
        {
            (void)fprintf(
                stderr,
                "pal_connection: automatic cleanup failed (status %d)\n",
                status);
            return;
        }
    }
}

/**
 * @brief      Creates shared synchronization and queue state for a service.
 *
 * @details    Initializes the per-service mutex and bounded event queue before
 *             any worker thread is started.
 *
 * @param[in,out] connection  Service object to initialize.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 *
 * @note       The queue retains at most PAL_CONNECTION_EVENT_QUEUE_CAPACITY
 *             receive events.
 * @warning    Partially initialized state must be released by the caller.
 */
static int32_t pal_connection_initialize_service(pal_connection_t *connection)
{
    int32_t status = pal_mutex_create(&connection->mutex);

    if (status != PAL_OK)
    {
        return status;
    }
    status = pal_queue_create(
        PAL_CONNECTION_EVENT_QUEUE_CAPACITY,
        &connection->events);
    if (status != PAL_OK)
    {
        (void)pal_mutex_destroy(connection->mutex);
        connection->mutex = NULL;
    }
    return status;
}

/**
 * @brief      Releases service-owned queue, mutex, socket, and memory.
 *
 * @details    Drains and frees queued receive events after workers have been
 *             joined, then releases the socket and synchronization objects.
 *
 * @param[in]  connection  Service whose resources are released.
 *
 * @return     The socket close status, or PAL_OK if no socket remains.
 *
 * @note       Worker threads must already be stopped before this function.
 * @warning    The connection pointer is invalid after this call.
 */
static int32_t pal_connection_destroy_service(pal_connection_t *connection)
{
    int32_t status = PAL_OK;

    if (connection->events != NULL)
    {
        void *event = NULL;

        while (pal_queue_pop(connection->events, &event, PAL_NO_WAIT) == PAL_OK)
        {
            pal_connection_event_free((pal_connection_event_t *)event);
        }
        (void)pal_queue_destroy(connection->events);
    }
    if (connection->socket != NULL)
    {
        status = pal_socket_close(connection->socket);
    }
    if (connection->mutex != NULL)
    {
        (void)pal_mutex_destroy(connection->mutex);
    }
    free(connection);
    return status;
}

/**
 * @brief      Stops a service, joins its worker, and releases owned resources.
 *
 * @details    Sets the stopping flag before joining. Workers use finite socket
 *             poll intervals, and a TCP listener worker joins all accepted
 *             peer workers before it returns.
 *
 * @param[in]  connection  Service to shut down.
 *
 * @return     Socket close status or PAL_OK.
 *
 * @note       Used both by explicit close and the atexit registry.
 * @warning    Calling from the service's own callback would self-join.
 */
static int32_t pal_connection_close_internal(
    pal_connection_t *connection,
    bool *released)
{
    int32_t status;

    *released = false;
    status = pal_mutex_lock(connection->mutex);

    if (status != PAL_OK)
    {
        return status;
    }
    connection->stopping = true;
    status = pal_mutex_unlock(connection->mutex);
    if (status != PAL_OK)
    {
        return status;
    }

    if (connection->worker != NULL)
    {
        status = pal_thread_join(connection->worker, NULL);
        if (status != PAL_OK)
        {
            return status;
        }
        connection->worker = NULL;
    }
    *released = true;
    return pal_connection_destroy_service(connection);
}

/**
 * @brief      Allocates and initializes a public connection service object.
 *
 * @details    Creates the common service state, including synchronization and
 *             the receive-event queue, before opening the native socket.
 *
 * @param[in]  transport  TCP or UDP transport.
 * @param[in]  role       Server or client role.
 *
 * @return     Initialized service or NULL if allocation fails.
 *
 * @note       Socket creation is performed by the public open functions.
 * @warning    Callers must clean up initialized fields on later failure.
 */
static pal_connection_t *pal_connection_allocate(
    pal_connection_transport_t transport,
    pal_connection_role_t role,
    int32_t *status)
{
    pal_connection_t *connection =
        (pal_connection_t *)calloc(1u, sizeof(*connection));

    if (status == NULL)
    {
        free(connection);
        return NULL;
    }
    if (connection == NULL)
    {
        *status = PAL_ERR_OUT_OF_MEMORY;
        return NULL;
    }

    connection->transport = transport;
    connection->role = role;
    connection->worker_status = PAL_OK;
    *status = pal_connection_initialize_service(connection);
    if (*status != PAL_OK)
    {
        free(connection);
        return NULL;
    }
    return connection;
}

/**
 * @brief      Opens and starts an autonomous TCP or UDP server.
 *
 * @details    Creates and binds the server socket, starts TCP listening when
 *             needed, registers the service for exit cleanup, and launches its
 *             receive or accept worker.
 *
 * @param[in]  transport      TCP or UDP transport.
 * @param[in]  local_address  Local address and port to bind.
 * @param[in]  callback       Optional receive callback.
 * @param[in]  user_data      Opaque pointer passed to callback.
 * @param[out] connection     Receives the running server service.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                       Server started.
 * @retval     PAL_ERR_NULL_POINTER         Address or output is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT     Transport or address is invalid.
 * @retval     PAL_ERR_OUT_OF_MEMORY        Service allocation failed.
 * @retval     PAL_ERR_ADDRESS_IN_USE       The local port is already in use.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED   Worker or exit-handler creation failed.
 * @retval     PAL_ERR_IO                   Socket setup failed.
 *
 * @note       Received data is available through both callback and event queue.
 * @warning    TCP server callbacks must use event peer_id to reply.
 */
int32_t pal_connection_open_server(
    pal_connection_transport_t transport,
    const struct pal_socket_address *local_address,
    pal_connection_event_callback_t callback,
    void *user_data,
    pal_connection_t **connection)
{
    pal_connection_t *created;
    pal_socket_type_t socket_type;
    int32_t status;

    (void)pal_connection_log_initialize();
    if (connection == NULL || local_address == NULL)
    {
        pal_connection_log_write(
            "open_server",
            NULL,
            transport,
            "server",
            0u,
            PAL_ERR_NULL_POINTER,
            NULL,
            0u);
        return PAL_ERR_NULL_POINTER;
    }
    *connection = NULL;
    if (transport != PAL_CONNECTION_TRANSPORT_TCP &&
        transport != PAL_CONNECTION_TRANSPORT_UDP)
    {
        pal_connection_log_write(
            "open_server",
            NULL,
            transport,
            "server",
            0u,
            PAL_ERR_INVALID_ARGUMENT,
            NULL,
            0u);
        return PAL_ERR_INVALID_ARGUMENT;
    }

    socket_type = (transport == PAL_CONNECTION_TRANSPORT_TCP)
        ? PAL_SOCKET_TYPE_TCP
        : PAL_SOCKET_TYPE_UDP;
    created = pal_connection_allocate(
        transport,
        (transport == PAL_CONNECTION_TRANSPORT_TCP)
            ? PAL_CONNECTION_ROLE_TCP_LISTENER
            : PAL_CONNECTION_ROLE_UDP_SERVER,
        &status);
    if (created == NULL)
    {
        pal_connection_log_write(
            "open_server",
            NULL,
            transport,
            "server",
            0u,
            status,
            NULL,
            0u);
        return status;
    }
    created->callback = callback;
    created->user_data = user_data;

    status = pal_socket_create(
        local_address->family,
        socket_type,
        &created->socket);
    if (status == PAL_OK)
    {
        status = pal_socket_bind(created->socket, local_address);
    }
    if (status == PAL_OK && transport == PAL_CONNECTION_TRANSPORT_TCP)
    {
        status = pal_socket_listen(
            created->socket,
            PAL_CONNECTION_DEFAULT_BACKLOG);
    }
    if (status == PAL_OK)
    {
        status = pal_connection_register(created);
    }
    if (status == PAL_OK)
    {
        status = pal_thread_create(
            (transport == PAL_CONNECTION_TRANSPORT_TCP)
                ? pal_connection_tcp_server_worker
                : pal_connection_receive_worker,
            created,
            &created->worker);
    }
    if (status != PAL_OK)
    {
        (void)pal_connection_unregister(created);
        (void)pal_connection_destroy_service(created);
        pal_connection_log_write(
            "open_server",
            NULL,
            transport,
            "server",
            0u,
            status,
            NULL,
            0u);
        return status;
    }

    *connection = created;
    pal_connection_log_service(
        "open_server", NULL, created, 0u, PAL_OK, NULL, 0u);
    return PAL_OK;
}

/**
 * @brief      Opens and starts an autonomous TCP or UDP client.
 *
 * @details    Creates the client socket, connects TCP clients, stores the UDP
 *             destination, registers exit cleanup, and starts a receive worker.
 *
 * @param[in]  transport       TCP or UDP transport.
 * @param[in]  remote_address  Remote server address.
 * @param[in]  callback        Optional receive callback.
 * @param[in]  user_data       Opaque pointer passed to callback.
 * @param[out] connection      Receives the running client service.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                       Client started.
 * @retval     PAL_ERR_NULL_POINTER         Address or output is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT     Transport or address is invalid.
 * @retval     PAL_ERR_OUT_OF_MEMORY        Service allocation failed.
 * @retval     PAL_ERR_CONNECTION_REFUSED   The TCP peer refused the connection.
 * @retval     PAL_ERR_NETWORK_UNREACHABLE  No route to the remote TCP peer.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED   Worker or exit-handler creation failed.
 * @retval     PAL_ERR_IO                   Socket setup or connect failed.
 *
 * @note       TCP establishment waits indefinitely; UDP is not OS-connected.
 * @warning    The callback executes on the receive worker and must return.
 */
int32_t pal_connection_open_client(
    pal_connection_transport_t transport,
    const struct pal_socket_address *remote_address,
    pal_connection_event_callback_t callback,
    void *user_data,
    pal_connection_t **connection)
{
    pal_connection_t *created;
    pal_socket_type_t socket_type;
    int32_t status;

    (void)pal_connection_log_initialize();
    if (connection == NULL || remote_address == NULL)
    {
        pal_connection_log_write(
            "open_client",
            NULL,
            transport,
            "client",
            0u,
            PAL_ERR_NULL_POINTER,
            NULL,
            0u);
        return PAL_ERR_NULL_POINTER;
    }
    *connection = NULL;
    if (transport != PAL_CONNECTION_TRANSPORT_TCP &&
        transport != PAL_CONNECTION_TRANSPORT_UDP)
    {
        pal_connection_log_write(
            "open_client",
            NULL,
            transport,
            "client",
            0u,
            PAL_ERR_INVALID_ARGUMENT,
            NULL,
            0u);
        return PAL_ERR_INVALID_ARGUMENT;
    }

    socket_type = (transport == PAL_CONNECTION_TRANSPORT_TCP)
        ? PAL_SOCKET_TYPE_TCP
        : PAL_SOCKET_TYPE_UDP;
    created = pal_connection_allocate(
        transport,
        (transport == PAL_CONNECTION_TRANSPORT_TCP)
            ? PAL_CONNECTION_ROLE_TCP_CLIENT
            : PAL_CONNECTION_ROLE_UDP_CLIENT,
        &status);
    if (created == NULL)
    {
        pal_connection_log_write(
            "open_client",
            NULL,
            transport,
            "client",
            0u,
            status,
            NULL,
            0u);
        return status;
    }
    created->callback = callback;
    created->user_data = user_data;
    if (transport == PAL_CONNECTION_TRANSPORT_UDP)
    {
        created->remote_address = *(const pal_socket_address_t *)remote_address;
    }

    status = pal_socket_create(
        remote_address->family,
        socket_type,
        &created->socket);
    if (status == PAL_OK && transport == PAL_CONNECTION_TRANSPORT_TCP)
    {
        status = pal_socket_connect(
            created->socket,
            (const pal_socket_address_t *)remote_address,
            PAL_WAIT_FOREVER);
    }
    if (status == PAL_OK)
    {
        status = pal_connection_register(created);
    }
    if (status == PAL_OK)
    {
        status = pal_thread_create(
            pal_connection_receive_worker,
            created,
            &created->worker);
    }
    if (status != PAL_OK)
    {
        (void)pal_connection_unregister(created);
        (void)pal_connection_destroy_service(created);
        pal_connection_log_write(
            "open_client",
            NULL,
            transport,
            "client",
            0u,
            status,
            NULL,
            0u);
        return status;
    }

    *connection = created;
    pal_connection_log_service(
        "open_client", NULL, created, 0u, PAL_OK, NULL, 0u);
    return PAL_OK;
}

/**
 * @brief      Sends bytes using a client, accepted TCP peer, or UDP service.
 *
 * @details    TCP clients require peer_id zero. TCP server replies locate the
 *             active accepted peer by identifier. UDP clients use their
 *             configured destination and UDP servers use their last sender.
 *
 * @param[in]  connection  Client or server service.
 * @param[in]  peer_id     TCP server peer identifier, or zero for other roles.
 * @param[in]  buffer      Bytes to send.
 * @param[in]  length      Number of bytes to send.
 * @param[out] bytes_sent  Receives the number of bytes sent.
 * @param[in]  timeout_ms  Maximum send readiness wait in milliseconds.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   Send operation completed.
 * @retval     PAL_ERR_NULL_POINTER     Connection or byte count is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Role or peer identifier is invalid.
 * @retval     PAL_ERR_NOT_FOUND        TCP peer is no longer active.
 * @retval     PAL_ERR_INVALID_STATE    UDP server has not received a datagram.
 * @retval     PAL_ERR_TIMEOUT          Socket did not become writable in time.
 * @retval     PAL_ERR_IO               Native send operation failed.
 *
 * @note       TCP can send fewer bytes than requested.
 * @warning    Concurrent callers are serialized per service or TCP peer.
 */
int32_t pal_connection_send(
    pal_connection_t *connection,
    uint64_t peer_id,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms)
{
    int32_t status = PAL_ERR_INVALID_ARGUMENT;
    size_t transferred = 0u;

    if (connection == NULL || bytes_sent == NULL)
    {
        pal_connection_log_write(
            "send",
            "out",
            (connection != NULL)
                ? connection->transport
                : (pal_connection_transport_t)0,
            (connection != NULL)
                ? pal_connection_role_name(connection->role)
                : "unknown",
            peer_id,
            PAL_ERR_NULL_POINTER,
            NULL,
            0u);
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_sent = 0u;

    if (connection->role == PAL_CONNECTION_ROLE_TCP_LISTENER)
    {
        pal_connection_t *peer;

        if (peer_id == 0u)
        {
            status = PAL_ERR_INVALID_ARGUMENT;
            goto log_send;
        }
        status = pal_mutex_lock(connection->mutex);
        if (status != PAL_OK)
        {
            goto log_send;
        }
        for (peer = connection->peer_next;
             peer != NULL && peer->peer_id != peer_id;
             peer = peer->peer_next)
        {
        }
        if (peer == NULL)
        {
            (void)pal_mutex_unlock(connection->mutex);
            status = PAL_ERR_NOT_FOUND;
            goto log_send;
        }
        status = pal_socket_send(
            peer->socket,
            buffer,
            length,
            &transferred,
            timeout_ms);
        (void)pal_mutex_unlock(connection->mutex);
        goto log_send;
    }

    if (peer_id != 0u)
    {
        status = PAL_ERR_INVALID_ARGUMENT;
        goto log_send;
    }
    if (connection->role == PAL_CONNECTION_ROLE_TCP_CLIENT)
    {
        status = pal_socket_send(
            connection->socket,
            buffer,
            length,
            &transferred,
            timeout_ms);
        goto log_send;
    }
    if (connection->role == PAL_CONNECTION_ROLE_UDP_CLIENT)
    {
        status = pal_socket_send_to(
            connection->socket,
            &connection->remote_address,
            buffer,
            length,
            &transferred,
            timeout_ms);
        goto log_send;
    }
    if (connection->role == PAL_CONNECTION_ROLE_UDP_SERVER)
    {
        pal_socket_address_t destination;

        status = pal_mutex_lock(connection->mutex);
        if (status != PAL_OK)
        {
            goto log_send;
        }
        if (!connection->has_last_peer)
        {
            (void)pal_mutex_unlock(connection->mutex);
            status = PAL_ERR_INVALID_STATE;
            goto log_send;
        }
        destination = connection->last_peer_address;
        (void)pal_mutex_unlock(connection->mutex);
        status = pal_socket_send_to(
            connection->socket,
            &destination,
            buffer,
            length,
            &transferred,
            timeout_ms);
        goto log_send;
    }

log_send:
    *bytes_sent = transferred;
    pal_connection_log_service(
        "send",
        "out",
        connection,
        peer_id,
        status,
        buffer,
        (buffer != NULL) ? transferred : 0u);
    return status;
}

/**
 * @brief      Waits for the next queued receive event.
 *
 * @details    Removes one event from the service's bounded FIFO event queue
 *             using the requested wait duration.
 *
 * @param[in]  connection  Service whose receive queue is read.
 * @param[in]  timeout_ms  PAL_NO_WAIT, PAL_WAIT_FOREVER, or finite milliseconds.
 * @param[out] event       Receives an owned event allocation.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   An event was returned.
 * @retval     PAL_ERR_NULL_POINTER     Service or output is NULL.
 * @retval     PAL_ERR_WOULD_BLOCK      Queue is empty and timeout is zero.
 * @retval     PAL_ERR_TIMEOUT          No event arrived before timeout.
 *
 * @note       Release successful results with pal_connection_event_release().
 * @warning    Do not call after closing the service.
 */
int32_t pal_connection_next_event(
    pal_connection_t *connection,
    uint32_t timeout_ms,
    pal_connection_event_t **event)
{
    void *queued_event = NULL;
    int32_t status;

    if (connection == NULL || event == NULL)
    {
        pal_connection_log_write(
            "next_event",
            NULL,
            (connection != NULL)
                ? connection->transport
                : (pal_connection_transport_t)0,
            (connection != NULL)
                ? pal_connection_role_name(connection->role)
                : "unknown",
            0u,
            PAL_ERR_NULL_POINTER,
            NULL,
            0u);
        return PAL_ERR_NULL_POINTER;
    }
    *event = NULL;
    status = pal_queue_pop(connection->events, &queued_event, timeout_ms);
    if (status == PAL_OK)
    {
        *event = (pal_connection_event_t *)queued_event;
    }
    pal_connection_log_service(
        "next_event", NULL, connection, 0u, status, NULL, 0u);
    return status;
}

/**
 * @brief      Releases an event returned by the event queue.
 *
 * @details    Frees the copied receive data and event wrapper allocated by the
 *             connection service.
 *
 * @param[in]  event  Event to release.
 *
 * @return     This function does not return a value.
 *
 * @note       A NULL pointer is ignored.
 * @warning    Do not access the event or its data after releasing it.
 */
void pal_connection_event_release(pal_connection_event_t *event)
{
    pal_connection_log_write(
        "release_event",
        NULL,
        (pal_connection_transport_t)0,
        "unknown",
        (event != NULL) ? event->peer_id : 0u,
        PAL_OK,
        NULL,
        0u);
    pal_connection_event_free(event);
}

/**
 * @brief      Retrieves the count of receive events discarded on queue overflow.
 *
 * @details    Reads the service's cumulative event-drop counter under its
 *             mutex.
 *
 * @param[in]  connection  Service to query.
 * @param[out] count       Receives the cumulative discarded event count.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Counter retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Service or output is NULL.
 * @retval     PAL_ERR_UNKNOWN        Service mutex operation failed.
 *
 * @note       The counter is reset only when the service is destroyed.
 * @warning    The value can increase immediately after it is read.
 */
int32_t pal_connection_get_dropped_event_count(
    pal_connection_t *connection,
    uint64_t *count)
{
    int32_t status;

    if (connection == NULL || count == NULL)
    {
        pal_connection_log_write(
            "get_dropped_event_count",
            NULL,
            (connection != NULL)
                ? connection->transport
                : (pal_connection_transport_t)0,
            (connection != NULL)
                ? pal_connection_role_name(connection->role)
                : "unknown",
            0u,
            PAL_ERR_NULL_POINTER,
            NULL,
            0u);
        return PAL_ERR_NULL_POINTER;
    }
    status = pal_mutex_lock(connection->mutex);
    if (status != PAL_OK)
    {
        pal_connection_log_service(
            "get_dropped_event_count", NULL, connection, 0u, status, NULL, 0u);
        return status;
    }
    *count = connection->dropped_events;
    (void)pal_mutex_unlock(connection->mutex);
    pal_connection_log_service(
        "get_dropped_event_count", NULL, connection, 0u, PAL_OK, NULL, 0u);
    return PAL_OK;
}

/**
 * @brief      Retrieves the first unexpected background worker error.
 *
 * @details    Reads the saved PAL status under the service mutex. A healthy or
 *             not-yet-failed worker reports PAL_OK.
 *
 * @param[in]  connection     Service to inspect.
 * @param[out] worker_status  Receives the recorded worker status.
 *
 * @return     PAL_OK when the status is retrieved or a negative PAL error.
 * @retval     PAL_OK                 Worker status retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Service or output is NULL.
 * @retval     PAL_ERR_UNKNOWN        Service mutex operation failed.
 *
 * @note       The first worker failure is retained until service destruction.
 * @warning    A worker can fail immediately after this status is read.
 */
int32_t pal_connection_get_worker_status(
    pal_connection_t *connection,
    int32_t *worker_status)
{
    int32_t status;

    if (connection == NULL || worker_status == NULL)
    {
        pal_connection_log_write(
            "get_worker_status",
            NULL,
            (connection != NULL)
                ? connection->transport
                : (pal_connection_transport_t)0,
            (connection != NULL)
                ? pal_connection_role_name(connection->role)
                : "unknown",
            0u,
            PAL_ERR_NULL_POINTER,
            NULL,
            0u);
        return PAL_ERR_NULL_POINTER;
    }
    status = pal_mutex_lock(connection->mutex);
    if (status != PAL_OK)
    {
        pal_connection_log_service(
            "get_worker_status", NULL, connection, 0u, status, NULL, 0u);
        return status;
    }
    *worker_status = connection->worker_status;
    (void)pal_mutex_unlock(connection->mutex);
    pal_connection_log_service(
        "get_worker_status", NULL, connection, 0u, PAL_OK, NULL, 0u);
    return PAL_OK;
}

/**
 * @brief      Retrieves the local address of a connection service.
 *
 * @details    Delegates to the underlying PAL socket to retrieve its current
 *             local address and assigned port.
 *
 * @param[in]  connection  Service to query.
 * @param[out] address     Receives the local address.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Local address retrieved.
 * @retval     PAL_ERR_NULL_POINTER   Service or output is NULL.
 * @retval     PAL_ERR_IO             Native address query failed.
 *
 * @note       Useful for servers opened with a zero port.
 * @warning    Do not call after service close.
 */
int32_t pal_connection_get_local_address(
    pal_connection_t *connection,
    struct pal_socket_address *address)
{
    if (connection == NULL || address == NULL)
    {
        pal_connection_log_write(
            "get_local_address",
            NULL,
            (connection != NULL)
                ? connection->transport
                : (pal_connection_transport_t)0,
            (connection != NULL)
                ? pal_connection_role_name(connection->role)
                : "unknown",
            0u,
            PAL_ERR_NULL_POINTER,
            NULL,
            0u);
        return PAL_ERR_NULL_POINTER;
    }
    {
        int32_t status = pal_socket_get_local_address(
        connection->socket,
        (pal_socket_address_t *)address);

        pal_connection_log_service(
            "get_local_address", NULL, connection, 0u, status, NULL, 0u);
        return status;
    }
}

/**
 * @brief      Stops a service and releases all associated resources.
 *
 * @details    Removes the service from the process-exit registry, signals its
 *             workers to stop, joins workers, closes sockets, discards pending
 *             queue events, and destroys synchronization objects.
 *
 * @param[in]  connection  Service to close.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Service resources were released.
 * @retval     PAL_ERR_NULL_POINTER   Connection is NULL.
 * @retval     PAL_ERR_IO             Native socket close failed.
 *
 * @note       Normal process exit performs the same cleanup automatically.
 * @warning    Do not close a service from its receive callback.
 */
int32_t pal_connection_close(pal_connection_t *connection)
{
    pal_connection_transport_t transport;
    pal_connection_role_t role;
    bool released = false;
    int32_t status;

    if (connection == NULL)
    {
        pal_connection_log_write(
            "close",
            NULL,
            (pal_connection_transport_t)0,
            "unknown",
            0u,
            PAL_ERR_NULL_POINTER,
            NULL,
            0u);
        return PAL_ERR_NULL_POINTER;
    }
    transport = connection->transport;
    role = connection->role;
    status = pal_connection_unregister(connection);
    if (status != PAL_OK)
    {
        pal_connection_log_service("close", NULL, connection, 0u, status, NULL, 0u);
        return status;
    }
    status = pal_connection_close_internal(connection, &released);
    if (status != PAL_OK && !released)
    {
        int32_t register_status = pal_connection_register(connection);

        if (register_status != PAL_OK)
        {
            pal_connection_log_service(
                "close", NULL, connection, 0u, register_status, NULL, 0u);
            return register_status;
        }
    }
    if (released)
    {
        pal_connection_log_write(
            "close",
            NULL,
            transport,
            pal_connection_role_name(role),
            0u,
            status,
            NULL,
            0u);
    }
    else
    {
        pal_connection_log_service(
            "close", NULL, connection, 0u, status, NULL, 0u);
    }
    return status;
}
