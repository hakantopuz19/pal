/**
 * @file pal_socket.h
 * @brief Public TCP and UDP socket interface for the PAL.
 */

#ifndef PAL_SOCKET_H
#define PAL_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pal_socket pal_socket_t;

typedef enum pal_socket_family
{
    PAL_SOCKET_FAMILY_IPV4 = 4,
    PAL_SOCKET_FAMILY_IPV6 = 6
} pal_socket_family_t;

typedef enum pal_socket_type
{
    PAL_SOCKET_TYPE_TCP = 1,
    PAL_SOCKET_TYPE_UDP = 2
} pal_socket_type_t;

typedef enum pal_socket_shutdown_mode
{
    PAL_SOCKET_SHUTDOWN_READ = 1,
    PAL_SOCKET_SHUTDOWN_WRITE = 2,
    PAL_SOCKET_SHUTDOWN_BOTH = 3
} pal_socket_shutdown_mode_t;

/* IP octets are stored in network order; port and scope_id are in host order.
 * IPv4 uses address[0..3] and ignores scope_id; IPv6 uses all bytes and uses
 * scope_id for interface-scoped addresses such as link-local addresses. */
typedef struct pal_socket_address
{
    pal_socket_family_t family;
    uint16_t port;
    uint8_t address[16];
    uint32_t scope_id;
} pal_socket_address_t;

/* Creates an IPv4 or IPv6 TCP or UDP socket. */
int32_t pal_socket_create(
    pal_socket_family_t family,
    pal_socket_type_t type,
    pal_socket_t **socket);

/* Binds a socket to a local address. Use port zero to request an ephemeral port. */
int32_t pal_socket_bind(pal_socket_t *socket, const pal_socket_address_t *address);

/* Starts listening for TCP connections. */
int32_t pal_socket_listen(pal_socket_t *socket, int32_t backlog);

/* Accepts a TCP connection, waiting up to timeout_ms for an incoming client.
 * The peer address output is optional. */
int32_t pal_socket_accept(
    pal_socket_t *socket,
    uint32_t timeout_ms,
    pal_socket_t **accepted_socket,
    pal_socket_address_t *peer_address);

/* Connects a TCP socket to a remote address, waiting up to timeout_ms. */
int32_t pal_socket_connect(
    pal_socket_t *socket,
    const pal_socket_address_t *address,
    uint32_t timeout_ms);

/* Include pal_types.h to use PAL_NO_WAIT or PAL_WAIT_FOREVER.
 * Sends or receives a TCP byte stream. A successful call may transfer fewer
 * bytes than requested; the transferred byte count is returned via the output.
 * timeout_ms is PAL_NO_WAIT, PAL_WAIT_FOREVER, or a finite timeout in ms. */
int32_t pal_socket_send(
    pal_socket_t *socket,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms);
int32_t pal_socket_receive(
    pal_socket_t *socket,
    void *buffer,
    size_t capacity,
    size_t *bytes_received,
    uint32_t timeout_ms);

/* Sends or receives one UDP datagram. The received source address is optional;
 * a successful receive can return zero bytes for an empty datagram. */
int32_t pal_socket_send_to(
    pal_socket_t *socket,
    const pal_socket_address_t *address,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms);
int32_t pal_socket_receive_from(
    pal_socket_t *socket,
    void *buffer,
    size_t capacity,
    size_t *bytes_received,
    pal_socket_address_t *source_address,
    uint32_t timeout_ms);

/* Retrieves the current local address, including an OS-assigned port. */
int32_t pal_socket_get_local_address(
    pal_socket_t *socket,
    pal_socket_address_t *address);

/* Shuts down one or both directions of a connected TCP socket. */
int32_t pal_socket_shutdown(
    pal_socket_t *socket,
    pal_socket_shutdown_mode_t mode);

/* Closes a socket and releases its resources. */
int32_t pal_socket_close(pal_socket_t *socket);

#ifdef __cplusplus
}
#endif

#endif
