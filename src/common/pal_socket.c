/**
 * @file pal_socket.c
 * @brief Platform-independent TCP and UDP socket API implementation.
 */

#include <stdlib.h>

#include "pal_error.h"
#include "pal_socket.h"
#include "pal_backend.h"

struct pal_socket
{
    void *backend_state;
    pal_socket_family_t family;
    pal_socket_type_t type;
};

/**
 * @brief      Creates a PAL socket for IPv4 or IPv6 TCP or UDP communication.
 *
 * @details    Allocates the public socket wrapper and delegates native socket
 *             creation to the selected platform backend. The output is set to
 *             NULL before creation begins.
 *
 * @param[in]  family  Address family, either IPv4 or IPv6.
 * @param[in]  type    Socket protocol type, either TCP or UDP.
 * @param[out] socket  Receives the created socket.
 *
 * @return     PAL_OK when the socket is created, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                   Socket creation succeeded.
 * @retval     PAL_ERR_NULL_POINTER     The output parameter is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT The address family or socket type is
 *                                      unsupported.
 * @retval     PAL_ERR_OUT_OF_MEMORY    PAL wrapper allocation failed.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED The operating system could not create
 *                                        the native socket.
 *
 * @note       The caller must release the socket with pal_socket_close().
 * @warning    The socket is initially unbound and unconnected.
 */
int32_t pal_socket_create(
    pal_socket_family_t family,
    pal_socket_type_t type,
    pal_socket_t **socket)
{
    pal_socket_t *created;
    int32_t status;

    if (socket == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *socket = NULL;

    if ((family != PAL_SOCKET_FAMILY_IPV4 && family != PAL_SOCKET_FAMILY_IPV6) ||
        (type != PAL_SOCKET_TYPE_TCP && type != PAL_SOCKET_TYPE_UDP))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    created = (pal_socket_t *)calloc(1u, sizeof(*created));
    if (created == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }

    status = pal_socket_backend_create(family, type, &created->backend_state);
    if (status != PAL_OK)
    {
        free(created);
        return status;
    }

    created->family = family;
    created->type = type;
    *socket = created;
    return PAL_OK;
}

/**
 * @brief      Binds a socket to a local IPv4 or IPv6 address.
 *
 * @details    Delegates address conversion and native bind operation to the
 *             platform backend. A port of zero requests an OS-assigned port.
 *
 * @param[in]  socket   Socket to bind.
 * @param[in]  address  Local address and port.
 *
 * @return     PAL_OK when binding succeeds, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                   Socket was bound.
 * @retval     PAL_ERR_NULL_POINTER     The socket or address is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT The address family does not match.
 * @retval     PAL_ERR_ACCESS_DENIED    The address cannot be bound.
 * @retval     PAL_ERR_IO               The native bind operation failed.
 *
 * @note       IPv4 addresses use the first four bytes of address in network
 *             octet order; IPv6 addresses use all sixteen bytes.
 * @warning    A socket can normally be bound only once.
 */
int32_t pal_socket_bind(pal_socket_t *socket, const pal_socket_address_t *address)
{
    if (socket == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket->family != address->family)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    return pal_socket_backend_bind(socket->backend_state, address);
}

/**
 * @brief      Starts listening for incoming TCP connections.
 *
 * @details    Configures a bound TCP socket to accept incoming connections
 *             using the platform's native listen operation.
 *
 * @param[in]  socket   Bound TCP socket.
 * @param[in]  backlog  Maximum pending connection count; must be positive.
 *
 * @return     PAL_OK when listening starts, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                   Socket is listening.
 * @retval     PAL_ERR_NULL_POINTER     The socket is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Backlog is not positive or socket is
 *                                      not TCP.
 * @retval     PAL_ERR_INVALID_STATE    Socket is not bound.
 * @retval     PAL_ERR_IO               Native listen operation failed.
 *
 * @note       The effective backlog may be limited by the operating system.
 * @warning    The socket must not be used for outgoing TCP connect after
 *             listening starts.
 */
int32_t pal_socket_listen(pal_socket_t *socket, int32_t backlog)
{
    if (socket == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket->type != PAL_SOCKET_TYPE_TCP || backlog <= 0)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    return pal_socket_backend_listen(socket->backend_state, backlog);
}

/**
 * @brief      Accepts an incoming TCP connection.
 *
 * @details    Waits for an incoming connection for the requested duration,
 *             creates a PAL socket for the accepted native connection, and
 *             optionally returns the remote peer address.
 *
 * @param[in]  socket           Listening TCP socket.
 * @param[in]  timeout_ms       Maximum wait in milliseconds.
 * @param[out] accepted_socket  Receives the connected socket.
 * @param[out] peer_address     Receives the peer address; may be NULL.
 *
 * @return     PAL_OK when a connection is accepted, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                   Connection was accepted.
 * @retval     PAL_ERR_NULL_POINTER     Listener or output socket is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not TCP.
 * @retval     PAL_ERR_WOULD_BLOCK      No connection was pending and timeout
 *                                      is zero.
 * @retval     PAL_ERR_TIMEOUT          No connection arrived before timeout.
 * @retval     PAL_ERR_OUT_OF_MEMORY    PAL socket allocation failed.
 * @retval     PAL_ERR_IO               Native accept operation failed.
 *
 * @note       Use PAL_WAIT_FOREVER to wait without a time limit.
 * @warning    The listener must remain valid while this call is active.
 */
int32_t pal_socket_accept(
    pal_socket_t *socket,
    uint32_t timeout_ms,
    pal_socket_t **accepted_socket,
    pal_socket_address_t *peer_address)
{
    pal_socket_t *accepted;
    void *backend_state = NULL;
    int32_t status;

    if (socket == NULL || accepted_socket == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *accepted_socket = NULL;
    if (socket->type != PAL_SOCKET_TYPE_TCP)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    accepted = (pal_socket_t *)calloc(1u, sizeof(*accepted));
    if (accepted == NULL)
    {
        return PAL_ERR_OUT_OF_MEMORY;
    }

    status = pal_socket_backend_accept(
        socket->backend_state,
        timeout_ms,
        &backend_state,
        peer_address);
    if (status != PAL_OK)
    {
        free(accepted);
        return status;
    }

    accepted->backend_state = backend_state;
    accepted->family = socket->family;
    accepted->type = PAL_SOCKET_TYPE_TCP;
    *accepted_socket = accepted;
    return PAL_OK;
}

/**
 * @brief      Connects a TCP socket to a remote endpoint.
 *
 * @details    Starts a native non-blocking connect and waits for completion
 *             according to the requested timeout.
 *
 * @param[in]  socket      TCP socket to connect.
 * @param[in]  address     Remote IPv4 or IPv6 endpoint.
 * @param[in]  timeout_ms  Maximum connection time in milliseconds.
 *
 * @return     PAL_OK when connected, otherwise a negative PAL error code.
 * @retval     PAL_OK                   Connection succeeded.
 * @retval     PAL_ERR_NULL_POINTER     Socket or address is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket type or address family mismatch.
 * @retval     PAL_ERR_WOULD_BLOCK      Connection is pending and timeout is
 *                                      zero.
 * @retval     PAL_ERR_TIMEOUT          Connection did not complete in time.
 * @retval     PAL_ERR_CONNECTION_REFUSED The remote endpoint refused the
 *                                        connection.
 * @retval     PAL_ERR_NETWORK_UNREACHABLE No route to the remote endpoint.
 * @retval     PAL_ERR_IO               Native connect operation failed.
 *
 * @note       Use PAL_WAIT_FOREVER to wait without a time limit.
 * @warning    A socket that has failed to connect should be closed rather than
 *             reused for another connection attempt.
 */
int32_t pal_socket_connect(
    pal_socket_t *socket,
    const pal_socket_address_t *address,
    uint32_t timeout_ms)
{
    if (socket == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket->type != PAL_SOCKET_TYPE_TCP || socket->family != address->family)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    return pal_socket_backend_connect(socket->backend_state, address, timeout_ms);
}

/**
 * @brief      Sends bytes over a connected TCP socket.
 *
 * @details    Sends up to @p length bytes and stores the number actually sent.
 *             Stream sockets may send fewer bytes than requested.
 *
 * @param[in]  socket      Connected TCP socket.
 * @param[in]  buffer      Data to send; may be NULL only when length is zero.
 * @param[in]  length      Number of bytes to send.
 * @param[out] bytes_sent  Receives the number of bytes sent.
 * @param[in]  timeout_ms  Maximum wait for send readiness in milliseconds.
 *
 * @return     PAL_OK when a send operation completes, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                   Send operation completed.
 * @retval     PAL_ERR_NULL_POINTER     Socket or byte-count output is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not TCP or buffer is NULL
 *                                      for a nonzero length.
 * @retval     PAL_ERR_WOULD_BLOCK      Socket is not writable and timeout is
 *                                      zero.
 * @retval     PAL_ERR_TIMEOUT          Socket did not become writable in time.
 * @retval     PAL_ERR_IO               Native send operation failed.
 *
 * @note       A successful call can send fewer bytes than requested.
 * @warning    The socket must be connected and remain open during the call.
 */
int32_t pal_socket_send(
    pal_socket_t *socket,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms)
{
    if (socket == NULL || bytes_sent == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_sent = 0u;
    if (socket->type != PAL_SOCKET_TYPE_TCP || (buffer == NULL && length != 0u))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    return pal_socket_backend_send(
        socket->backend_state, buffer, length, bytes_sent, timeout_ms);
}

/**
 * @brief      Receives bytes from a connected TCP socket.
 *
 * @details    Reads up to @p capacity bytes and stores the number actually
 *             received. A zero-capacity call is a no-op; otherwise a
 *             successful zero-byte result indicates orderly peer shutdown.
 *
 * @param[in]  socket          Connected TCP socket.
 * @param[out] buffer          Receives data; may be NULL only when capacity is zero.
 * @param[in]  capacity        Maximum number of bytes to receive.
 * @param[out] bytes_received  Receives the number of bytes read.
 * @param[in]  timeout_ms      Maximum wait for receive readiness in ms.
 *
 * @return     PAL_OK when a receive operation completes, otherwise a negative
 *             PAL error code.
 * @retval     PAL_OK                   Receive operation completed.
 * @retval     PAL_ERR_NULL_POINTER     Socket or byte-count output is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not TCP or buffer is NULL
 *                                      for nonzero capacity.
 * @retval     PAL_ERR_WOULD_BLOCK      No data is available and timeout is zero.
 * @retval     PAL_ERR_TIMEOUT          No data arrived before timeout.
 * @retval     PAL_ERR_IO               Native receive operation failed.
 *
 * @note       A zero-byte successful result means the peer closed its sending
 *             direction.
 * @warning    The socket must be connected and remain open during the call.
 */
int32_t pal_socket_receive(
    pal_socket_t *socket,
    void *buffer,
    size_t capacity,
    size_t *bytes_received,
    uint32_t timeout_ms)
{
    if (socket == NULL || bytes_received == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_received = 0u;
    if (socket->type != PAL_SOCKET_TYPE_TCP ||
        (buffer == NULL && capacity != 0u))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    return pal_socket_backend_receive(
        socket->backend_state, buffer, capacity, bytes_received, timeout_ms);
}

/**
 * @brief      Sends one UDP datagram to a remote endpoint.
 *
 * @details    Sends one datagram with its destination address. The socket may
 *             be bound or unbound; an unbound socket is assigned a local
 *             endpoint by the operating system.
 *
 * @param[in]  socket      UDP socket.
 * @param[in]  address     Destination address and port.
 * @param[in]  buffer      Datagram contents.
 * @param[in]  length      Datagram size in bytes.
 * @param[out] bytes_sent  Receives the datagram size sent.
 * @param[in]  timeout_ms  Maximum wait for send readiness in milliseconds.
 *
 * @return     PAL_OK when the datagram is sent, otherwise a negative PAL error
 *             code.
 * @retval     PAL_OK                   Datagram was sent.
 * @retval     PAL_ERR_NULL_POINTER     Socket, address, or output is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket family mismatch or invalid
 *                                      buffer/length combination.
 * @retval     PAL_ERR_WOULD_BLOCK      Socket is not writable and timeout is
 *                                      zero.
 * @retval     PAL_ERR_TIMEOUT          Socket did not become writable in time.
 * @retval     PAL_ERR_IO               Native send operation failed.
 *
 * @note       A datagram is sent as one message; partial datagram sends are
 *             reported as an I/O error.
 * @warning    Datagram delivery and ordering are not guaranteed by UDP.
 */
int32_t pal_socket_send_to(
    pal_socket_t *socket,
    const pal_socket_address_t *address,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms)
{
    if (socket == NULL || address == NULL || bytes_sent == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_sent = 0u;
    if (socket->type != PAL_SOCKET_TYPE_UDP ||
        socket->family != address->family ||
        (buffer == NULL && length != 0u))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    return pal_socket_backend_send_to(
        socket->backend_state, address, buffer, length, bytes_sent, timeout_ms);
}

/**
 * @brief      Receives one UDP datagram.
 *
 * @details    Receives a single datagram into the provided buffer and reports
 *             its source address when requested. Datagrams larger than the
 *             buffer may be truncated by the operating system.
 *
 * @param[in]  socket           Bound UDP socket.
 * @param[out] buffer           Receives the datagram contents.
 * @param[in]  capacity         Maximum number of bytes to receive.
 * @param[out] bytes_received   Receives the number of bytes copied.
 * @param[out] source_address   Receives sender address; may be NULL.
 * @param[in]  timeout_ms       Maximum wait in milliseconds.
 *
 * @return     PAL_OK when a datagram is received, otherwise a negative PAL
 *             error code.
 * @retval     PAL_OK                   A datagram was received.
 * @retval     PAL_ERR_NULL_POINTER     Socket, buffer for nonzero capacity, or
 *                                      byte-count output is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not UDP.
 * @retval     PAL_ERR_WOULD_BLOCK      No datagram is available and timeout is
 *                                      zero.
 * @retval     PAL_ERR_TIMEOUT          No datagram arrived before timeout.
 * @retval     PAL_ERR_IO               Native receive operation failed.
 *
 * @note       A zero-byte datagram is valid; use PAL_OK to distinguish it from
 *             a failed receive.
 * @warning    The socket must remain open during the call.
 */
int32_t pal_socket_receive_from(
    pal_socket_t *socket,
    void *buffer,
    size_t capacity,
    size_t *bytes_received,
    pal_socket_address_t *source_address,
    uint32_t timeout_ms)
{
    if (socket == NULL || bytes_received == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_received = 0u;
    if (socket->type != PAL_SOCKET_TYPE_UDP ||
        (buffer == NULL && capacity != 0u))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    return pal_socket_backend_receive_from(
        socket->backend_state,
        buffer,
        capacity,
        bytes_received,
        source_address,
        timeout_ms);
}

/**
 * @brief      Retrieves a socket's current local address.
 *
 * @details    Returns the address and port assigned by bind or by automatic
 *             binding during a connection or datagram send.
 *
 * @param[in]  socket   Socket to query.
 * @param[out] address  Receives the local address.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   Local address was retrieved.
 * @retval     PAL_ERR_NULL_POINTER     Socket or output is NULL.
 * @retval     PAL_ERR_IO               Native address query failed.
 *
 * @note       An unbound socket may report a wildcard address and port zero.
 * @warning    The socket must remain open during the call.
 */
int32_t pal_socket_get_local_address(
    pal_socket_t *socket,
    pal_socket_address_t *address)
{
    if (socket == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    return pal_socket_backend_get_local_address(socket->backend_state, address);
}

/**
 * @brief      Shuts down one or both directions of a TCP connection.
 *
 * @details    Prevents further reads, writes, or both on a connected TCP socket
 *             according to the selected shutdown mode.
 *
 * @param[in]  socket  Connected TCP socket.
 * @param[in]  mode    Read, write, or both directions.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   Shutdown succeeded.
 * @retval     PAL_ERR_NULL_POINTER     The socket parameter is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not TCP or mode is invalid.
 * @retval     PAL_ERR_INVALID_STATE    Socket is not connected.
 * @retval     PAL_ERR_IO               Native shutdown operation failed.
 *
 * @note       Shutdown does not release the socket handle; call
 *             pal_socket_close() to close it.
 * @warning    Shutdown cannot be reversed.
 */
int32_t pal_socket_shutdown(
    pal_socket_t *socket,
    pal_socket_shutdown_mode_t mode)
{
    if (socket == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket->type != PAL_SOCKET_TYPE_TCP ||
        (mode != PAL_SOCKET_SHUTDOWN_READ &&
         mode != PAL_SOCKET_SHUTDOWN_WRITE &&
         mode != PAL_SOCKET_SHUTDOWN_BOTH))
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    return pal_socket_backend_shutdown(socket->backend_state, mode);
}

/**
 * @brief      Closes a socket and releases its PAL resources.
 *
 * @details    Closes the native socket through its platform backend and frees
 *             the public wrapper only after native close succeeds.
 *
 * @param[in]  socket  Socket to close.
 *
 * @return     PAL_OK when closed, otherwise a negative PAL error code.
 * @retval     PAL_OK                   Socket was closed.
 * @retval     PAL_ERR_NULL_POINTER     The socket parameter is NULL.
 * @retval     PAL_ERR_IO               Native close operation failed.
 *
 * @note       Closing a TCP socket may discard pending data unless the
 *             application performs an appropriate shutdown protocol.
 * @warning    The socket pointer is invalid after a successful close.
 */
int32_t pal_socket_close(pal_socket_t *socket)
{
    int32_t status;

    if (socket == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    status = pal_socket_backend_close(socket->backend_state);
    if (status != PAL_OK)
    {
        return status;
    }

    free(socket);
    return PAL_OK;
}
