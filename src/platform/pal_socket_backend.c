/**
 * @file pal_socket_backend.c
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
#error "PAL sockets require Windows Vista or newer."
#endif

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pal_error.h"
#include "pal_backend.h"
#include "pal_socket.h"

typedef struct pal_windows_socket_state
{
    SOCKET descriptor;
    pal_socket_family_t family;
    pal_socket_type_t type;
    int connected;
    int listening;
} pal_windows_socket_state_t;

static INIT_ONCE g_pal_winsock_once = INIT_ONCE_STATIC_INIT;

/**
 * @brief      Initializes Winsock for the process.
 *
 * @details    Calls WSAStartup() once through the Windows init-once mechanism
 *             and stores its native result for all socket creations.
 *
 * @param[in]  once       Init-once object supplied by Windows.
 * @param[in]  parameter  Reserved callback argument.
 * @param[out] context    Receives reserved callback context data.
 *
 * @return     TRUE when Winsock initialization succeeds; otherwise FALSE.
 *
 * @note       Winsock remains initialized for process lifetime.
 * @warning    Do not call WSACleanup() while PAL sockets may still be active.
 */
static BOOL CALLBACK pal_windows_winsock_initialize(
    PINIT_ONCE once,
    PVOID parameter,
    PVOID *context)
{
    WSADATA data;

    (void)once;
    (void)parameter;
    (void)context;
    return (WSAStartup(MAKEWORD(2, 2), &data) == 0);
}

/**
 * @brief      Maps a Winsock error to a PAL status.
 *
 * @details    Converts common network errors to dedicated PAL status values
 *             and maps other Winsock failures to PAL_ERR_IO.
 *
 * @param[in]  error_code  WSA error code from a failed socket operation.
 *
 * @return     PAL status corresponding to the Winsock error.
 *
 * @note       Native Winsock error numbers are never returned directly.
 * @warning    Pass the saved WSAGetLastError() value from the failed call.
 */
static int32_t pal_windows_socket_error(int error_code)
{
    switch (error_code)
    {
        case WSAEADDRINUSE:
            return PAL_ERR_ADDRESS_IN_USE;
        case WSAECONNREFUSED:
            return PAL_ERR_CONNECTION_REFUSED;
        case WSAENETUNREACH:
        case WSAEHOSTUNREACH:
            return PAL_ERR_NETWORK_UNREACHABLE;
        case WSAENOTCONN:
            return PAL_ERR_NOT_CONNECTED;
        case WSAEACCES:
            return PAL_ERR_ACCESS_DENIED;
        case WSAEWOULDBLOCK:
            return PAL_ERR_WOULD_BLOCK;
        default:
            return PAL_ERR_IO;
    }
}

/**
 * @brief      Converts a PAL address to a native Winsock address.
 *
 * @details    Copies IP octets to sockaddr_in or sockaddr_in6 and converts the
 *             host-order port to network byte order.
 *
 * @param[in]  address     PAL address to convert.
 * @param[out] native      Receives native address storage.
 * @param[out] native_len  Receives native address length.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   Address conversion succeeded.
 * @retval     PAL_ERR_NULL_POINTER     An input or output pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Address family is unsupported.
 *
 * @note       IPv4 uses the first four address bytes; IPv6 uses all sixteen.
 * @warning    Address bytes must be in network octet order.
 */
static int32_t pal_windows_to_sockaddr(
    const pal_socket_address_t *address,
    SOCKADDR_STORAGE *native,
    int *native_len)
{
    if (address == NULL || native == NULL || native_len == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    memset(native, 0, sizeof(*native));
    if (address->family == PAL_SOCKET_FAMILY_IPV4)
    {
        SOCKADDR_IN *ipv4 = (SOCKADDR_IN *)native;

        ipv4->sin_family = AF_INET;
        ipv4->sin_port = htons(address->port);
        memcpy(&ipv4->sin_addr, address->address, 4u);
        *native_len = (int)sizeof(*ipv4);
        return PAL_OK;
    }
    if (address->family == PAL_SOCKET_FAMILY_IPV6)
    {
        SOCKADDR_IN6 *ipv6 = (SOCKADDR_IN6 *)native;

        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_port = htons(address->port);
        ipv6->sin6_scope_id = address->scope_id;
        memcpy(&ipv6->sin6_addr, address->address, 16u);
        *native_len = (int)sizeof(*ipv6);
        return PAL_OK;
    }

    return PAL_ERR_INVALID_ARGUMENT;
}

/**
 * @brief      Converts a native Winsock address to PAL format.
 *
 * @details    Copies address octets and converts the port to host byte order.
 *
 * @param[in]  native      Native address storage.
 * @param[in]  native_len  Length of the native address.
 * @param[out] address     Receives the converted address.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   Address conversion succeeded.
 * @retval     PAL_ERR_NULL_POINTER     An input or output pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Native address family is unsupported.
 *
 * @note       The output structure is cleared before use.
 * @warning    The native buffer must contain a complete supported address.
 */
static int32_t pal_windows_from_sockaddr(
    const SOCKADDR_STORAGE *native,
    int native_len,
    pal_socket_address_t *address)
{
    if (native == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    memset(address, 0, sizeof(*address));
    if (native->ss_family == AF_INET && native_len >= (int)sizeof(SOCKADDR_IN))
    {
        const SOCKADDR_IN *ipv4 = (const SOCKADDR_IN *)native;

        address->family = PAL_SOCKET_FAMILY_IPV4;
        address->port = ntohs(ipv4->sin_port);
        memcpy(address->address, &ipv4->sin_addr, 4u);
        return PAL_OK;
    }
    if (native->ss_family == AF_INET6 && native_len >= (int)sizeof(SOCKADDR_IN6))
    {
        const SOCKADDR_IN6 *ipv6 = (const SOCKADDR_IN6 *)native;

        address->family = PAL_SOCKET_FAMILY_IPV6;
        address->port = ntohs(ipv6->sin6_port);
        address->scope_id = ipv6->sin6_scope_id;
        memcpy(address->address, &ipv6->sin6_addr, 16u);
        return PAL_OK;
    }

    return PAL_ERR_INVALID_ARGUMENT;
}

/**
 * @brief      Waits for a Winsock descriptor to become ready.
 *
 * @details    Uses select() with an optional timeout. Timeout values are
 *             rounded up to microseconds and capped to the maximum supported
 *             DWORD duration when necessary.
 *
 * @param[in]  descriptor  Native socket descriptor.
 * @param[in]  writable    TRUE to wait for write readiness; FALSE for read.
 * @param[in]  timeout_ms  Timeout in milliseconds or UINT32_MAX for infinity.
 *
 * @return     PAL_OK when ready or a negative PAL error status.
 * @retval     PAL_OK                 Socket reported readiness.
 * @retval     PAL_ERR_WOULD_BLOCK    Timeout is zero and socket is not ready.
 * @retval     PAL_ERR_TIMEOUT        Finite timeout elapsed.
 * @retval     PAL_ERR_IO             select() failed.
 *
 * @note       Windows ignores the first select() parameter.
 * @warning    The socket descriptor must remain valid while waiting.
 */
static int32_t pal_windows_wait(SOCKET descriptor, int writable, uint32_t timeout_ms)
{
    fd_set read_set;
    fd_set write_set;
    struct timeval duration;
    struct timeval *duration_pointer = NULL;
    int result;

    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    if (writable)
    {
        FD_SET(descriptor, &write_set);
    }
    else
    {
        FD_SET(descriptor, &read_set);
    }

    if (timeout_ms != UINT32_MAX)
    {
        duration.tv_sec = (long)(timeout_ms / 1000u);
        duration.tv_usec = (long)(timeout_ms % 1000u) * 1000L;
        duration_pointer = &duration;
    }

    result = select(0, &read_set, &write_set, NULL, duration_pointer);
    if (result > 0)
    {
        return PAL_OK;
    }
    if (result == 0)
    {
        return (timeout_ms == 0u) ? PAL_ERR_WOULD_BLOCK : PAL_ERR_TIMEOUT;
    }
    return pal_windows_socket_error(WSAGetLastError());
}
#elif defined(__linux__)
/* Linux backend implementation. */
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "pal_error.h"
#include "pal_backend.h"
#include "pal_socket.h"

typedef struct pal_linux_socket_state
{
    int descriptor;
    pal_socket_family_t family;
    pal_socket_type_t type;
    int connected;
    int listening;
} pal_linux_socket_state_t;

/**
 * @brief      Maps a native Linux socket error to a PAL status.
 *
 * @details    Converts commonly meaningful networking errors to their
 *             dedicated PAL status and maps all remaining errors to
 *             PAL_ERR_IO.
 *
 * @param[in]  error_code  errno value returned by a socket operation.
 *
 * @return     PAL status corresponding to the native socket error.
 *
 * @note       The mapping never returns a raw errno value.
 * @warning    The caller must pass the saved errno from the failed operation.
 */
static int32_t pal_linux_socket_error(int error_code)
{
    switch (error_code)
    {
        case EADDRINUSE:
            return PAL_ERR_ADDRESS_IN_USE;
        case ECONNREFUSED:
            return PAL_ERR_CONNECTION_REFUSED;
        case ENETUNREACH:
        case EHOSTUNREACH:
            return PAL_ERR_NETWORK_UNREACHABLE;
        case ENOTCONN:
            return PAL_ERR_NOT_CONNECTED;
        case EACCES:
        case EPERM:
            return PAL_ERR_ACCESS_DENIED;
        default:
            return PAL_ERR_IO;
    }
}

/**
 * @brief      Converts a PAL address to a native sockaddr structure.
 *
 * @details    Validates the address family, copies address octets, and
 *             converts the host-order port to network byte order.
 *
 * @param[in]  address     PAL address to convert.
 * @param[out] native      Receives the native address storage.
 * @param[out] native_len Receives the native address length.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   Address conversion succeeded.
 * @retval     PAL_ERR_NULL_POINTER     An input or output pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Address family is not IPv4 or IPv6.
 *
 * @note       IPv4 uses the first four address bytes; IPv6 uses all sixteen.
 * @warning    Address bytes must already be in network octet order.
 */
static int32_t pal_linux_to_sockaddr(
    const pal_socket_address_t *address,
    struct sockaddr_storage *native,
    socklen_t *native_len)
{
    if (address == NULL || native == NULL || native_len == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    memset(native, 0, sizeof(*native));
    if (address->family == PAL_SOCKET_FAMILY_IPV4)
    {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)native;

        ipv4->sin_family = AF_INET;
        ipv4->sin_port = htons(address->port);
        memcpy(&ipv4->sin_addr, address->address, 4u);
        *native_len = (socklen_t)sizeof(*ipv4);
        return PAL_OK;
    }
    if (address->family == PAL_SOCKET_FAMILY_IPV6)
    {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)native;

        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_port = htons(address->port);
        ipv6->sin6_scope_id = address->scope_id;
        memcpy(&ipv6->sin6_addr, address->address, 16u);
        *native_len = (socklen_t)sizeof(*ipv6);
        return PAL_OK;
    }

    return PAL_ERR_INVALID_ARGUMENT;
}

/**
 * @brief      Converts a native sockaddr structure to a PAL address.
 *
 * @details    Extracts the IP address octets and converts the port to host
 *             byte order.
 *
 * @param[in]  native      Native address storage to convert.
 * @param[in]  native_len  Length of the native address.
 * @param[out] address     Receives the converted PAL address.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   Address conversion succeeded.
 * @retval     PAL_ERR_NULL_POINTER     An input or output pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Native address family is unsupported.
 *
 * @note       The output structure is cleared before address fields are set.
 * @warning    The native buffer must contain a complete supported address.
 */
static int32_t pal_linux_from_sockaddr(
    const struct sockaddr_storage *native,
    socklen_t native_len,
    pal_socket_address_t *address)
{
    if (native == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }

    memset(address, 0, sizeof(*address));
    if (native->ss_family == AF_INET &&
        native_len >= (socklen_t)sizeof(struct sockaddr_in))
    {
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)native;

        address->family = PAL_SOCKET_FAMILY_IPV4;
        address->port = ntohs(ipv4->sin_port);
        memcpy(address->address, &ipv4->sin_addr, 4u);
        return PAL_OK;
    }
    if (native->ss_family == AF_INET6 &&
        native_len >= (socklen_t)sizeof(struct sockaddr_in6))
    {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)native;

        address->family = PAL_SOCKET_FAMILY_IPV6;
        address->port = ntohs(ipv6->sin6_port);
        address->scope_id = ipv6->sin6_scope_id;
        memcpy(address->address, &ipv6->sin6_addr, 16u);
        return PAL_OK;
    }

    return PAL_ERR_INVALID_ARGUMENT;
}

/**
 * @brief      Reads monotonic milliseconds for Linux socket timeouts.
 *
 * @details    Converts CLOCK_MONOTONIC to an unsigned millisecond count.
 *
 * @param[out] time_ms  Receives the current monotonic time.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Clock reading succeeded.
 * @retval     PAL_ERR_NULL_POINTER   The output pointer is NULL.
 * @retval     PAL_ERR_UNKNOWN        clock_gettime() failed.
 *
 * @note       The result is intended only for elapsed-time calculations.
 * @warning    The result is not wall-clock time.
 */
static int32_t pal_linux_monotonic_ms(uint64_t *time_ms)
{
    struct timespec now;

    if (time_ms == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return PAL_ERR_UNKNOWN;
    }

    *time_ms = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
    return PAL_OK;
}

/**
 * @brief      Waits for a Linux socket to become ready.
 *
 * @details    Polls until the requested event, an error, or the timeout. EINTR
 *             restarts polling without extending the original timeout.
 *
 * @param[in]  descriptor  Native socket descriptor.
 * @param[in]  events      Requested poll events.
 * @param[in]  timeout_ms  Timeout in milliseconds or UINT32_MAX for infinity.
 *
 * @return     PAL_OK when ready or a negative PAL error status.
 * @retval     PAL_OK                 Socket reported readiness.
 * @retval     PAL_ERR_WOULD_BLOCK    Timeout is zero and socket is not ready.
 * @retval     PAL_ERR_TIMEOUT        Finite timeout elapsed.
 * @retval     PAL_ERR_INTERRUPTED    Polling was interrupted.
 * @retval     PAL_ERR_IO             poll() failed.
 *
 * @note       Error and hangup events are returned to let the subsequent
 *             socket operation report the specific native failure.
 * @warning    The descriptor must remain open while this function is waiting.
 */
static int32_t pal_linux_wait(
    int descriptor,
    short events,
    uint32_t timeout_ms)
{
    struct pollfd descriptor_event;
    uint64_t start_ms = 0u;
    int32_t status;

    descriptor_event.fd = descriptor;
    descriptor_event.events = events;
    descriptor_event.revents = 0;

    if (timeout_ms != UINT32_MAX && timeout_ms != 0u)
    {
        status = pal_linux_monotonic_ms(&start_ms);
        if (status != PAL_OK)
        {
            return status;
        }
    }

    for (;;)
    {
        int wait_ms;
        int poll_result;

        if (timeout_ms == UINT32_MAX)
        {
            wait_ms = -1;
        }
        else if (timeout_ms == 0u)
        {
            wait_ms = 0;
        }
        else
        {
            uint64_t current_ms;
            uint64_t elapsed;

            status = pal_linux_monotonic_ms(&current_ms);
            if (status != PAL_OK)
            {
                return status;
            }
            elapsed = current_ms - start_ms;
            if (elapsed >= (uint64_t)timeout_ms)
            {
                return PAL_ERR_TIMEOUT;
            }
            wait_ms = (elapsed > (uint64_t)(INT_MAX - 1))
                ? INT_MAX
                : (int)(timeout_ms - (uint32_t)elapsed);
        }

        descriptor_event.revents = 0;
        poll_result = poll(&descriptor_event, 1u, wait_ms);
        if (poll_result > 0)
        {
            return PAL_OK;
        }
        if (poll_result == 0)
        {
            if (timeout_ms == 0u)
            {
                return PAL_ERR_WOULD_BLOCK;
            }
            continue;
        }
        if (errno != EINTR)
        {
            return pal_linux_socket_error(errno);
        }
        if (timeout_ms == 0u)
        {
            return PAL_ERR_INTERRUPTED;
        }
    }
}
#else
#error "PAL backend is implemented for Windows and Linux only."
#endif

/* Shared backend entry points with platform-specific implementations. */
#if defined(_WIN32)
/**
 * @brief      Creates a non-blocking Winsock TCP or UDP socket.
 *
 * @details    Initializes Winsock once, creates a native socket, enables
 *             non-blocking mode, and stores socket metadata in backend state.
 *
 * @param[in]  family  IPv4 or IPv6 address family.
 * @param[in]  type    TCP or UDP protocol type.
 * @param[out] state   Receives the backend state pointer.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                       Native socket was created.
 * @retval     PAL_ERR_NULL_POINTER         Output pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT     Address family or socket type is
 *                                          invalid.
 * @retval     PAL_ERR_OUT_OF_MEMORY        Backend state allocation failed.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED   Winsock initialization or socket
 *                                          creation failed.
 *
 * @note       Per-operation timeouts are implemented with select().
 * @warning    The descriptor remains non-blocking for its lifetime.
 */
#elif defined(__linux__)
/**
 * @brief      Creates a non-blocking Linux TCP or UDP socket.
 *
 * @details    Creates the native descriptor with close-on-exec and non-blocking
 *             flags, then records its family and type in backend state.
 *
 * @param[in]  family  IPv4 or IPv6 address family.
 * @param[in]  type    TCP or UDP protocol type.
 * @param[out] state   Receives the backend state pointer.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                       Native socket was created.
 * @retval     PAL_ERR_NULL_POINTER         Output pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT     Address family or socket type is
 *                                          invalid.
 * @retval     PAL_ERR_OUT_OF_MEMORY        Backend state allocation failed.
 * @retval     PAL_ERR_RESOURCE_EXHAUSTED   socket() failed.
 *
 * @note       Per-operation timeouts are implemented with poll().
 * @warning    The descriptor remains non-blocking for its lifetime.
 */
#endif
int32_t pal_socket_backend_create(
    pal_socket_family_t family,
    pal_socket_type_t type,
    void **state)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *created;
    int native_family;
    int native_type;
    u_long nonblocking = 1u;
    SOCKET descriptor;

    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *state = NULL;

    native_family = (family == PAL_SOCKET_FAMILY_IPV4)
        ? AF_INET
        : (family == PAL_SOCKET_FAMILY_IPV6) ? AF_INET6 : -1;
    native_type = (type == PAL_SOCKET_TYPE_TCP)
        ? SOCK_STREAM
        : (type == PAL_SOCKET_TYPE_UDP) ? SOCK_DGRAM : -1;
    if (native_family < 0 || native_type < 0)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    if (InitOnceExecuteOnce(
            &g_pal_winsock_once,
            pal_windows_winsock_initialize,
            NULL,
            NULL) == 0)
    {
        return PAL_ERR_RESOURCE_EXHAUSTED;
    }

    descriptor = socket(native_family, native_type, 0);
    if (descriptor == INVALID_SOCKET)
    {
        return PAL_ERR_RESOURCE_EXHAUSTED;
    }
    if (ioctlsocket(descriptor, FIONBIO, &nonblocking) != 0)
    {
        int error_code = WSAGetLastError();

        (void)closesocket(descriptor);
        return pal_windows_socket_error(error_code);
    }

    created = (pal_windows_socket_state_t *)calloc(1u, sizeof(*created));
    if (created == NULL)
    {
        (void)closesocket(descriptor);
        return PAL_ERR_OUT_OF_MEMORY;
    }
    created->descriptor = descriptor;
    created->family = family;
    created->type = type;
    *state = created;
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *created;
    int native_family;
    int native_type;
    int descriptor;
    int descriptor_flags;

    if (state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *state = NULL;

    native_family = (family == PAL_SOCKET_FAMILY_IPV4)
        ? AF_INET
        : (family == PAL_SOCKET_FAMILY_IPV6) ? AF_INET6 : -1;
    native_type = (type == PAL_SOCKET_TYPE_TCP)
        ? SOCK_STREAM
        : (type == PAL_SOCKET_TYPE_UDP) ? SOCK_DGRAM : -1;
    if (native_family < 0 || native_type < 0)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    descriptor = socket(native_family, native_type, 0);
    if (descriptor < 0)
    {
        return (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM)
            ? PAL_ERR_RESOURCE_EXHAUSTED
            : pal_linux_socket_error(errno);
    }

    descriptor_flags = fcntl(descriptor, F_GETFL, 0);
    if (descriptor_flags < 0 ||
        fcntl(descriptor, F_SETFL, descriptor_flags | O_NONBLOCK) < 0 ||
        fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0)
    {
        (void)close(descriptor);
        return pal_linux_socket_error(errno);
    }

    created = (pal_linux_socket_state_t *)calloc(1u, sizeof(*created));
    if (created == NULL)
    {
        (void)close(descriptor);
        return PAL_ERR_OUT_OF_MEMORY;
    }
    created->descriptor = descriptor;
    created->family = family;
    created->type = type;
    *state = created;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Binds a Winsock socket to a local address.
 *
 * @details    Converts the PAL address and invokes the native bind() function.
 *
 * @param[in]  state    Windows socket backend state.
 * @param[in]  address  Local address to bind.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   bind() succeeded.
 * @retval     PAL_ERR_INVALID_ARGUMENT Address family mismatch or invalid
 *                                      address.
 * @retval     PAL_ERR_ADDRESS_IN_USE   Address is already in use.
 * @retval     PAL_ERR_ACCESS_DENIED    Bind permission was denied.
 * @retval     PAL_ERR_IO               Other bind failure.
 *
 * @note       The address port is specified in host byte order.
 * @warning    A native socket can normally be bound only once.
 */
#elif defined(__linux__)
/**
 * @brief      Binds a native Linux socket to a local address.
 *
 * @details    Converts the PAL address and invokes bind() on the native
 *             non-blocking descriptor.
 *
 * @param[in]  state    Linux socket backend state.
 * @param[in]  address  Local address to bind.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   bind() succeeded.
 * @retval     PAL_ERR_INVALID_ARGUMENT Address family mismatch or invalid
 *                                      address.
 * @retval     PAL_ERR_ADDRESS_IN_USE   Address is already in use.
 * @retval     PAL_ERR_ACCESS_DENIED    Bind permission was denied.
 * @retval     PAL_ERR_IO               Other bind failure.
 *
 * @note       The address port is specified in host byte order.
 * @warning    A native socket cannot normally be bound more than once.
 */
#endif
int32_t pal_socket_backend_bind(void *state, const pal_socket_address_t *address)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;
    SOCKADDR_STORAGE native;
    int native_len;
    int32_t status;

    if (socket_state == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket_state->family != address->family)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    status = pal_windows_to_sockaddr(address, &native, &native_len);
    if (status != PAL_OK)
    {
        return status;
    }
    if (bind(
            socket_state->descriptor,
            (const SOCKADDR *)&native,
            native_len) == SOCKET_ERROR)
    {
        return pal_windows_socket_error(WSAGetLastError());
    }
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;
    struct sockaddr_storage native;
    socklen_t native_len;
    int32_t status;

    if (socket_state == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket_state->family != address->family)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_linux_to_sockaddr(address, &native, &native_len);
    if (status != PAL_OK)
    {
        return status;
    }
    if (bind(socket_state->descriptor, (struct sockaddr *)&native, native_len) != 0)
    {
        return pal_linux_socket_error(errno);
    }
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Starts listening on a Windows TCP socket.
 *
 * @details    Calls listen() with the requested positive pending-connection
 *             backlog and marks the backend state as listening.
 *
 * @param[in]  state    Windows socket backend state.
 * @param[in]  backlog  Maximum pending connection count.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   listen() succeeded.
 * @retval     PAL_ERR_NULL_POINTER     Backend state is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not TCP or backlog is invalid.
 * @retval     PAL_ERR_INVALID_STATE    Socket is not bound.
 * @retval     PAL_ERR_IO               listen() failed.
 *
 * @note       The OS may cap the requested backlog.
 * @warning    Do not use a listening socket for outgoing connect.
 */
#elif defined(__linux__)
/**
 * @brief      Starts listening on a Linux TCP socket.
 *
 * @details    Calls listen() with the requested positive pending-connection
 *             backlog and marks the backend state as listening.
 *
 * @param[in]  state    Linux socket backend state.
 * @param[in]  backlog  Maximum pending connection count.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                   listen() succeeded.
 * @retval     PAL_ERR_NULL_POINTER     Backend state is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not TCP or backlog is invalid.
 * @retval     PAL_ERR_INVALID_STATE    Socket is not bound.
 * @retval     PAL_ERR_IO               listen() failed.
 *
 * @note       The OS may cap the requested backlog.
 * @warning    Do not use this socket for outgoing connect after listen().
 */
#endif
int32_t pal_socket_backend_listen(void *state, int32_t backlog)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;

    if (socket_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket_state->type != PAL_SOCKET_TYPE_TCP || backlog <= 0)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    if (listen(socket_state->descriptor, (int)backlog) == SOCKET_ERROR)
    {
        return (WSAGetLastError() == WSAEINVAL)
            ? PAL_ERR_INVALID_STATE
            : pal_windows_socket_error(WSAGetLastError());
    }
    socket_state->listening = 1;
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;

    if (socket_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket_state->type != PAL_SOCKET_TYPE_TCP || backlog <= 0)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    if (listen(socket_state->descriptor, (int)backlog) != 0)
    {
        return (errno == EINVAL) ? PAL_ERR_INVALID_STATE : pal_linux_socket_error(errno);
    }
    socket_state->listening = 1;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Accepts a pending Windows TCP connection.
 *
 * @details    Waits for read readiness, accepts a native client socket, enables
 *             non-blocking mode, and returns backend state for the connection.
 *
 * @param[in]  state           Listening socket backend state.
 * @param[in]  timeout_ms      Maximum wait in milliseconds.
 * @param[out] accepted_state  Receives accepted backend state.
 * @param[out] peer_address    Receives peer address; may be NULL.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   Connection accepted.
 * @retval     PAL_ERR_NULL_POINTER     Listener or state output is NULL.
 * @retval     PAL_ERR_INVALID_STATE    Socket is not listening.
 * @retval     PAL_ERR_WOULD_BLOCK      No connection and zero timeout.
 * @retval     PAL_ERR_TIMEOUT          No connection before timeout.
 * @retval     PAL_ERR_OUT_OF_MEMORY    Accepted state allocation failed.
 * @retval     PAL_ERR_IO               accept() or descriptor setup failed.
 *
 * @note       Peer address output may be omitted.
 * @warning    The listener must remain valid while waiting.
 */
#elif defined(__linux__)
/**
 * @brief      Accepts a pending Linux TCP connection.
 *
 * @details    Waits for readability, accepts a native client descriptor, marks
 *             it non-blocking and close-on-exec, and returns it as backend
 *             state.
 *
 * @param[in]  state           Listening socket backend state.
 * @param[in]  timeout_ms      Maximum wait in milliseconds.
 * @param[out] accepted_state  Receives accepted backend state.
 * @param[out] peer_address    Receives peer address; may be NULL.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   Connection accepted.
 * @retval     PAL_ERR_NULL_POINTER     Listener or state output is NULL.
 * @retval     PAL_ERR_INVALID_STATE    Socket is not listening.
 * @retval     PAL_ERR_WOULD_BLOCK      No connection and zero timeout.
 * @retval     PAL_ERR_TIMEOUT          No connection before timeout.
 * @retval     PAL_ERR_OUT_OF_MEMORY    Accepted state allocation failed.
 * @retval     PAL_ERR_IO               accept() or descriptor setup failed.
 *
 * @note       Peer address output may be omitted.
 * @warning    The listener must remain valid while waiting.
 */
#endif
int32_t pal_socket_backend_accept(
    void *state,
    uint32_t timeout_ms,
    void **accepted_state,
    pal_socket_address_t *peer_address)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *listener = (pal_windows_socket_state_t *)state;
    pal_windows_socket_state_t *accepted;
    SOCKADDR_STORAGE peer;
    int peer_len = (int)sizeof(peer);
    u_long nonblocking = 1u;
    SOCKET descriptor;
    int32_t status;

    if (listener == NULL || accepted_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *accepted_state = NULL;
    if (!listener->listening)
    {
        return PAL_ERR_INVALID_STATE;
    }

    status = pal_windows_wait(listener->descriptor, 0, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }
    descriptor = accept(listener->descriptor, (SOCKADDR *)&peer, &peer_len);
    if (descriptor == INVALID_SOCKET)
    {
        return pal_windows_socket_error(WSAGetLastError());
    }
    if (ioctlsocket(descriptor, FIONBIO, &nonblocking) != 0)
    {
        int error_code = WSAGetLastError();

        (void)closesocket(descriptor);
        return pal_windows_socket_error(error_code);
    }

    accepted = (pal_windows_socket_state_t *)calloc(1u, sizeof(*accepted));
    if (accepted == NULL)
    {
        (void)closesocket(descriptor);
        return PAL_ERR_OUT_OF_MEMORY;
    }
    accepted->descriptor = descriptor;
    accepted->family = listener->family;
    accepted->type = PAL_SOCKET_TYPE_TCP;
    accepted->connected = 1;

    if (peer_address != NULL)
    {
        status = pal_windows_from_sockaddr(&peer, peer_len, peer_address);
        if (status != PAL_OK)
        {
            (void)closesocket(descriptor);
            free(accepted);
            return status;
        }
    }

    *accepted_state = accepted;
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *listener = (pal_linux_socket_state_t *)state;
    pal_linux_socket_state_t *accepted;
    struct sockaddr_storage peer;
    socklen_t peer_len = (socklen_t)sizeof(peer);
    int descriptor;
    int32_t status;

    if (listener == NULL || accepted_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *accepted_state = NULL;
    if (!listener->listening)
    {
        return PAL_ERR_INVALID_STATE;
    }

    status = pal_linux_wait(listener->descriptor, POLLIN, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }
    descriptor = accept(listener->descriptor, (struct sockaddr *)&peer, &peer_len);
    if (descriptor < 0)
    {
        return (errno == EAGAIN || errno == EWOULDBLOCK)
            ? PAL_ERR_WOULD_BLOCK
            : pal_linux_socket_error(errno);
    }
    if (fcntl(descriptor, F_SETFL, fcntl(descriptor, F_GETFL, 0) | O_NONBLOCK) < 0 ||
        fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0)
    {
        (void)close(descriptor);
        return pal_linux_socket_error(errno);
    }

    accepted = (pal_linux_socket_state_t *)calloc(1u, sizeof(*accepted));
    if (accepted == NULL)
    {
        (void)close(descriptor);
        return PAL_ERR_OUT_OF_MEMORY;
    }
    accepted->descriptor = descriptor;
    accepted->family = listener->family;
    accepted->type = PAL_SOCKET_TYPE_TCP;
    accepted->connected = 1;

    if (peer_address != NULL)
    {
        status = pal_linux_from_sockaddr(&peer, peer_len, peer_address);
        if (status != PAL_OK)
        {
            (void)close(descriptor);
            free(accepted);
            return status;
        }
    }

    *accepted_state = accepted;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Connects a Windows TCP socket with the requested timeout.
 *
 * @details    Starts a non-blocking connect, waits for write readiness, then
 *             checks SO_ERROR to confirm that the connection succeeded.
 *
 * @param[in]  state       TCP socket backend state.
 * @param[in]  address     Remote endpoint.
 * @param[in]  timeout_ms  Maximum connection time in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                       Connection succeeded.
 * @retval     PAL_ERR_NULL_POINTER         Socket or address is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT     Family mismatch or non-TCP socket.
 * @retval     PAL_ERR_WOULD_BLOCK          Connect is pending with zero timeout.
 * @retval     PAL_ERR_TIMEOUT              Connect did not complete in time.
 * @retval     PAL_ERR_CONNECTION_REFUSED   Remote endpoint refused connection.
 * @retval     PAL_ERR_NETWORK_UNREACHABLE  No route to endpoint.
 * @retval     PAL_ERR_IO                   Other connect error.
 *
 * @note       Completion is verified using getsockopt(SO_ERROR).
 * @warning    Close the socket after a failed or timed-out connection attempt.
 */
#elif defined(__linux__)
/**
 * @brief      Connects a Linux TCP socket with a finite or infinite timeout.
 *
 * @details    Initiates non-blocking connect(), waits for completion, then
 *             checks SO_ERROR to determine whether the connection succeeded.
 *
 * @param[in]  state       TCP socket backend state.
 * @param[in]  address     Remote endpoint.
 * @param[in]  timeout_ms  Maximum connection time in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                       Connection succeeded.
 * @retval     PAL_ERR_NULL_POINTER         Socket or address is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT     Family mismatch or non-TCP socket.
 * @retval     PAL_ERR_WOULD_BLOCK          Connect is pending with zero timeout.
 * @retval     PAL_ERR_TIMEOUT              Connect did not complete in time.
 * @retval     PAL_ERR_CONNECTION_REFUSED   Remote endpoint refused connection.
 * @retval     PAL_ERR_NETWORK_UNREACHABLE  No route to endpoint.
 * @retval     PAL_ERR_IO                   Other connect error.
 *
 * @note       Connect completion is verified with getsockopt(SO_ERROR).
 * @warning    Close the socket after a failed or timed-out connection attempt.
 */
#endif
int32_t pal_socket_backend_connect(
    void *state,
    const pal_socket_address_t *address,
    uint32_t timeout_ms)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;
    SOCKADDR_STORAGE native;
    int native_len;
    int socket_error = 0;
    int error_len = (int)sizeof(socket_error);
    int32_t status;

    if (socket_state == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket_state->type != PAL_SOCKET_TYPE_TCP ||
        socket_state->family != address->family)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    status = pal_windows_to_sockaddr(address, &native, &native_len);
    if (status != PAL_OK)
    {
        return status;
    }

    if (connect(
            socket_state->descriptor,
            (const SOCKADDR *)&native,
            native_len) == SOCKET_ERROR)
    {
        int error_code = WSAGetLastError();

        if (error_code != WSAEWOULDBLOCK && error_code != WSAEINPROGRESS &&
            error_code != WSAEALREADY)
        {
            return pal_windows_socket_error(error_code);
        }
        status = pal_windows_wait(socket_state->descriptor, 1, timeout_ms);
        if (status != PAL_OK)
        {
            return status;
        }
    }

    if (getsockopt(
            socket_state->descriptor,
            SOL_SOCKET,
            SO_ERROR,
            (char *)&socket_error,
            &error_len) == SOCKET_ERROR)
    {
        return pal_windows_socket_error(WSAGetLastError());
    }
    if (socket_error != 0)
    {
        return pal_windows_socket_error(socket_error);
    }

    socket_state->connected = 1;
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;
    struct sockaddr_storage native;
    socklen_t native_len;
    int32_t status;
    int socket_error = 0;
    socklen_t error_len = (socklen_t)sizeof(socket_error);

    if (socket_state == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket_state->type != PAL_SOCKET_TYPE_TCP ||
        socket_state->family != address->family)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_linux_to_sockaddr(address, &native, &native_len);
    if (status != PAL_OK)
    {
        return status;
    }
    if (connect(socket_state->descriptor, (struct sockaddr *)&native, native_len) != 0)
    {
        if (errno != EINPROGRESS && errno != EALREADY && errno != EWOULDBLOCK)
        {
            return pal_linux_socket_error(errno);
        }
        status = pal_linux_wait(socket_state->descriptor, POLLOUT, timeout_ms);
        if (status != PAL_OK)
        {
            return status;
        }
    }

    if (getsockopt(
            socket_state->descriptor,
            SOL_SOCKET,
            SO_ERROR,
            &socket_error,
            &error_len) != 0)
    {
        return pal_linux_socket_error(errno);
    }
    if (socket_error != 0)
    {
        return pal_linux_socket_error(socket_error);
    }

    socket_state->connected = 1;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Sends bytes using a connected Windows TCP socket.
 *
 * @details    Waits for write readiness and sends a non-blocking portion of
 *             the byte stream.
 *
 * @param[in]  state       Connected socket backend state.
 * @param[in]  buffer      Bytes to send.
 * @param[in]  length      Number of requested bytes.
 * @param[out] bytes_sent  Receives the number sent.
 * @param[in]  timeout_ms  Maximum wait in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   send() completed.
 * @retval     PAL_ERR_NULL_POINTER     Required pointer is NULL.
 * @retval     PAL_ERR_NOT_CONNECTED    Socket is not connected.
 * @retval     PAL_ERR_WOULD_BLOCK      Socket is not writable immediately.
 * @retval     PAL_ERR_TIMEOUT          Socket did not become writable in time.
 * @retval     PAL_ERR_IO               send() failed.
 *
 * @note       A successful call may send fewer bytes than requested.
 * @warning    The caller retains ownership of the input buffer.
 */
#elif defined(__linux__)
/**
 * @brief      Sends bytes using a connected Linux TCP socket.
 *
 * @details    Waits for POLLOUT using the requested timeout and sends a
 *             non-blocking portion of the byte stream.
 *
 * @param[in]  state       Connected socket backend state.
 * @param[in]  buffer      Bytes to send.
 * @param[in]  length      Number of requested bytes.
 * @param[out] bytes_sent  Receives the number sent.
 * @param[in]  timeout_ms  Maximum wait in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   Send syscall completed.
 * @retval     PAL_ERR_NULL_POINTER     Required pointer is NULL.
 * @retval     PAL_ERR_INVALID_STATE    Socket is not connected.
 * @retval     PAL_ERR_WOULD_BLOCK      Socket is not writable immediately.
 * @retval     PAL_ERR_TIMEOUT          Socket did not become writable in time.
 * @retval     PAL_ERR_IO               send() failed.
 *
 * @note       A successful call may send fewer bytes than requested.
 * @warning    The caller retains ownership of the input buffer.
 */
#endif
int32_t pal_socket_backend_send(
    void *state,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;
    int result;
    int32_t status;
    int chunk;

    if (socket_state == NULL || bytes_sent == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_sent = 0u;
    if (!socket_state->connected)
    {
        return PAL_ERR_NOT_CONNECTED;
    }
    if (length == 0u)
    {
        return PAL_OK;
    }

    status = pal_windows_wait(socket_state->descriptor, 1, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }
    chunk = (length > (size_t)INT_MAX) ? INT_MAX : (int)length;
    result = send(socket_state->descriptor, (const char *)buffer, chunk, 0);
    if (result == SOCKET_ERROR)
    {
        return pal_windows_socket_error(WSAGetLastError());
    }

    *bytes_sent = (size_t)result;
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;
    ssize_t result;
    int32_t status;

    if (socket_state == NULL || bytes_sent == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_sent = 0u;
    if (!socket_state->connected)
    {
        return PAL_ERR_NOT_CONNECTED;
    }
    if (length == 0u)
    {
        return PAL_OK;
    }

    status = pal_linux_wait(socket_state->descriptor, POLLOUT, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }
    result = send(socket_state->descriptor, buffer, length, MSG_NOSIGNAL);
    if (result < 0)
    {
        return (errno == EAGAIN || errno == EWOULDBLOCK)
            ? PAL_ERR_WOULD_BLOCK
            : pal_linux_socket_error(errno);
    }

    *bytes_sent = (size_t)result;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Receives bytes using a connected Windows TCP socket.
 *
 * @details    Waits for read readiness and receives a non-blocking portion of
 *             the byte stream.
 *
 * @param[in]  state           Connected socket backend state.
 * @param[out] buffer          Receives bytes.
 * @param[in]  capacity        Maximum number of bytes to receive.
 * @param[out] bytes_received  Receives the number read.
 * @param[in]  timeout_ms      Maximum wait in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   recv() completed, including peer EOF.
 * @retval     PAL_ERR_NULL_POINTER     Required pointer is NULL.
 * @retval     PAL_ERR_NOT_CONNECTED    Socket is not connected.
 * @retval     PAL_ERR_WOULD_BLOCK      No data is immediately available.
 * @retval     PAL_ERR_TIMEOUT          No data arrived before timeout.
 * @retval     PAL_ERR_IO               recv() failed.
 *
 * @note       A successful zero-byte read indicates peer shutdown unless the
 *             requested capacity was zero.
 * @warning    The caller retains ownership of the output buffer.
 */
#elif defined(__linux__)
/**
 * @brief      Receives bytes using a connected Linux TCP socket.
 *
 * @details    Waits for POLLIN using the requested timeout and receives a
 *             non-blocking portion of the byte stream.
 *
 * @param[in]  state           Connected socket backend state.
 * @param[out] buffer          Receives bytes.
 * @param[in]  capacity        Maximum number of bytes to receive.
 * @param[out] bytes_received  Receives the number read.
 * @param[in]  timeout_ms      Maximum wait in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   recv() completed, including peer EOF.
 * @retval     PAL_ERR_NULL_POINTER     Required pointer is NULL.
 * @retval     PAL_ERR_NOT_CONNECTED    Socket is not connected.
 * @retval     PAL_ERR_WOULD_BLOCK      No data is immediately available.
 * @retval     PAL_ERR_TIMEOUT          No data arrived before timeout.
 * @retval     PAL_ERR_IO               recv() failed.
 *
 * @note       A successful zero-byte read indicates peer shutdown unless the
 *             requested capacity was zero.
 * @warning    The caller retains ownership of the output buffer.
 */
#endif
int32_t pal_socket_backend_receive(
    void *state,
    void *buffer,
    size_t capacity,
    size_t *bytes_received,
    uint32_t timeout_ms)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;
    int result;
    int32_t status;
    int chunk;

    if (socket_state == NULL || bytes_received == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_received = 0u;
    if (!socket_state->connected)
    {
        return PAL_ERR_NOT_CONNECTED;
    }
    if (capacity == 0u)
    {
        return PAL_OK;
    }

    status = pal_windows_wait(socket_state->descriptor, 0, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }
    chunk = (capacity > (size_t)INT_MAX) ? INT_MAX : (int)capacity;
    result = recv(socket_state->descriptor, (char *)buffer, chunk, 0);
    if (result == SOCKET_ERROR)
    {
        return pal_windows_socket_error(WSAGetLastError());
    }

    *bytes_received = (size_t)result;
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;
    ssize_t result;
    int32_t status;

    if (socket_state == NULL || bytes_received == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_received = 0u;
    if (!socket_state->connected)
    {
        return PAL_ERR_NOT_CONNECTED;
    }
    status = pal_linux_wait(socket_state->descriptor, POLLIN, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }
    result = recv(socket_state->descriptor, buffer, capacity, 0);
    if (result < 0)
    {
        return (errno == EAGAIN || errno == EWOULDBLOCK)
            ? PAL_ERR_WOULD_BLOCK
            : pal_linux_socket_error(errno);
    }

    *bytes_received = (size_t)result;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Sends one UDP datagram to a Windows socket address.
 *
 * @details    Waits for write readiness and submits one datagram with sendto().
 *
 * @param[in]  state       UDP socket backend state.
 * @param[in]  address     Destination address.
 * @param[in]  buffer      Datagram bytes.
 * @param[in]  length      Datagram length.
 * @param[out] bytes_sent  Receives bytes sent.
 * @param[in]  timeout_ms  Maximum wait in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   Datagram send completed.
 * @retval     PAL_ERR_NULL_POINTER     A required pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket type/family or datagram length
 *                                      is invalid.
 * @retval     PAL_ERR_WOULD_BLOCK      Socket is not writable immediately.
 * @retval     PAL_ERR_TIMEOUT          Socket did not become writable in time.
 * @retval     PAL_ERR_IO               sendto() failed or sent partial data.
 *
 * @note       UDP preserves datagram boundaries.
 * @warning    Successful transmission does not guarantee delivery.
 */
#elif defined(__linux__)
/**
 * @brief      Sends one UDP datagram to a Linux socket address.
 *
 * @details    Waits for socket writability and submits one datagram with
 *             sendto().
 *
 * @param[in]  state       UDP socket backend state.
 * @param[in]  address     Destination address.
 * @param[in]  buffer      Datagram bytes.
 * @param[in]  length      Datagram length.
 * @param[out] bytes_sent  Receives bytes sent.
 * @param[in]  timeout_ms  Maximum wait in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   Datagram send completed.
 * @retval     PAL_ERR_NULL_POINTER     A required pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket type or family is invalid.
 * @retval     PAL_ERR_WOULD_BLOCK      Socket is not writable immediately.
 * @retval     PAL_ERR_TIMEOUT          Socket did not become writable in time.
 * @retval     PAL_ERR_IO               sendto() failed or sent a partial
 *                                      datagram.
 *
 * @note       UDP preserves datagram boundaries.
 * @warning    Successful transmission does not guarantee delivery.
 */
#endif
int32_t pal_socket_backend_send_to(
    void *state,
    const pal_socket_address_t *address,
    const void *buffer,
    size_t length,
    size_t *bytes_sent,
    uint32_t timeout_ms)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;
    SOCKADDR_STORAGE native;
    int native_len;
    int result;
    int32_t status;

    if (socket_state == NULL || address == NULL || bytes_sent == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_sent = 0u;
    if (socket_state->type != PAL_SOCKET_TYPE_UDP ||
        socket_state->family != address->family ||
        length > (size_t)INT_MAX)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    status = pal_windows_to_sockaddr(address, &native, &native_len);
    if (status != PAL_OK)
    {
        return status;
    }
    status = pal_windows_wait(socket_state->descriptor, 1, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }
    result = sendto(
        socket_state->descriptor,
        (const char *)buffer,
        (int)length,
        0,
        (const SOCKADDR *)&native,
        native_len);
    if (result == SOCKET_ERROR)
    {
        return pal_windows_socket_error(WSAGetLastError());
    }
    if ((size_t)result != length)
    {
        return PAL_ERR_IO;
    }

    *bytes_sent = (size_t)result;
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;
    struct sockaddr_storage native;
    socklen_t native_len;
    ssize_t result;
    int32_t status;

    if (socket_state == NULL || address == NULL || bytes_sent == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_sent = 0u;
    if (socket_state->type != PAL_SOCKET_TYPE_UDP ||
        socket_state->family != address->family)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_linux_to_sockaddr(address, &native, &native_len);
    if (status != PAL_OK)
    {
        return status;
    }
    status = pal_linux_wait(socket_state->descriptor, POLLOUT, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }
    result = sendto(
        socket_state->descriptor,
        buffer,
        length,
        MSG_NOSIGNAL,
        (struct sockaddr *)&native,
        native_len);
    if (result < 0)
    {
        return (errno == EAGAIN || errno == EWOULDBLOCK)
            ? PAL_ERR_WOULD_BLOCK
            : pal_linux_socket_error(errno);
    }
    if ((size_t)result != length)
    {
        return PAL_ERR_IO;
    }

    *bytes_sent = (size_t)result;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Receives one UDP datagram from a Windows socket.
 *
 * @details    Waits for read readiness and receives one datagram with
 *             recvfrom(), optionally converting its source address.
 *
 * @param[in]  state            Bound UDP socket backend state.
 * @param[out] buffer           Receives datagram contents.
 * @param[in]  capacity         Maximum receive buffer size.
 * @param[out] bytes_received   Receives copied byte count.
 * @param[out] source_address   Receives sender address; may be NULL.
 * @param[in]  timeout_ms       Maximum wait in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   A datagram was received.
 * @retval     PAL_ERR_NULL_POINTER     A required pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not UDP.
 * @retval     PAL_ERR_WOULD_BLOCK      No datagram is immediately available.
 * @retval     PAL_ERR_TIMEOUT          No datagram arrived before timeout.
 * @retval     PAL_ERR_IO               recvfrom() failed.
 *
 * @note       Oversized datagrams are truncated to the supplied capacity.
 * @warning    The output buffer must remain writable for the duration.
 */
#elif defined(__linux__)
/**
 * @brief      Receives one UDP datagram from a Linux socket.
 *
 * @details    Waits for readability and calls recvfrom(), optionally converting
 *             the sender address to PAL format.
 *
 * @param[in]  state            Bound UDP socket backend state.
 * @param[out] buffer           Receives datagram contents.
 * @param[in]  capacity         Maximum receive buffer size.
 * @param[out] bytes_received   Receives copied byte count.
 * @param[out] source_address   Receives sender address; may be NULL.
 * @param[in]  timeout_ms       Maximum wait in milliseconds.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   A datagram was received.
 * @retval     PAL_ERR_NULL_POINTER     A required pointer is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not UDP.
 * @retval     PAL_ERR_WOULD_BLOCK      No datagram is immediately available.
 * @retval     PAL_ERR_TIMEOUT          No datagram arrived before timeout.
 * @retval     PAL_ERR_IO               recvfrom() failed.
 *
 * @note       Oversized datagrams are truncated to the supplied capacity.
 * @warning    The output buffer must remain writable for the duration.
 */
#endif
int32_t pal_socket_backend_receive_from(
    void *state,
    void *buffer,
    size_t capacity,
    size_t *bytes_received,
    pal_socket_address_t *source_address,
    uint32_t timeout_ms)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;
    SOCKADDR_STORAGE source;
    int source_len = (int)sizeof(source);
    int result;
    int32_t status;
    int chunk;

    if (socket_state == NULL || bytes_received == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_received = 0u;
    if (socket_state->type != PAL_SOCKET_TYPE_UDP)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    status = pal_windows_wait(socket_state->descriptor, 0, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }

    chunk = (capacity > (size_t)INT_MAX) ? INT_MAX : (int)capacity;
    result = recvfrom(
        socket_state->descriptor,
        (char *)buffer,
        chunk,
        0,
        (SOCKADDR *)&source,
        &source_len);
    if (result == SOCKET_ERROR)
    {
        return pal_windows_socket_error(WSAGetLastError());
    }
    if (source_address != NULL)
    {
        status = pal_windows_from_sockaddr(&source, source_len, source_address);
        if (status != PAL_OK)
        {
            return status;
        }
    }

    *bytes_received = (size_t)result;
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;
    struct sockaddr_storage source;
    socklen_t source_len = (socklen_t)sizeof(source);
    ssize_t result;
    int32_t status;

    if (socket_state == NULL || bytes_received == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    *bytes_received = 0u;
    if (socket_state->type != PAL_SOCKET_TYPE_UDP)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    status = pal_linux_wait(socket_state->descriptor, POLLIN, timeout_ms);
    if (status != PAL_OK)
    {
        return status;
    }
    result = recvfrom(
        socket_state->descriptor,
        buffer,
        capacity,
        0,
        (struct sockaddr *)&source,
        &source_len);
    if (result < 0)
    {
        return (errno == EAGAIN || errno == EWOULDBLOCK)
            ? PAL_ERR_WOULD_BLOCK
            : pal_linux_socket_error(errno);
    }
    if (source_address != NULL)
    {
        status = pal_linux_from_sockaddr(&source, source_len, source_address);
        if (status != PAL_OK)
        {
            return status;
        }
    }

    *bytes_received = (size_t)result;
    return PAL_OK;
#endif
}

#if defined(_WIN32)
/**
 * @brief      Retrieves the current local endpoint of a Windows socket.
 *
 * @details    Calls getsockname() and converts the resulting sockaddr to PAL
 *             address format.
 *
 * @param[in]  state    Windows socket backend state.
 * @param[out] address  Receives the local endpoint.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Local endpoint was retrieved.
 * @retval     PAL_ERR_NULL_POINTER   An input or output pointer is NULL.
 * @retval     PAL_ERR_IO             getsockname() failed.
 *
 * @note       Automatically assigned local ports are returned after connect or
 *             datagram send.
 * @warning    The socket must remain open during the call.
 */
#elif defined(__linux__)
/**
 * @brief      Retrieves the Linux socket's local endpoint.
 *
 * @details    Calls getsockname() and converts the returned sockaddr to PAL
 *             address format.
 *
 * @param[in]  state    Linux socket backend state.
 * @param[out] address  Receives the local endpoint.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Local endpoint was retrieved.
 * @retval     PAL_ERR_NULL_POINTER   An input or output pointer is NULL.
 * @retval     PAL_ERR_IO             getsockname() failed.
 *
 * @note       An automatically assigned port is returned after connect or send.
 * @warning    The socket state must remain valid during the call.
 */
#endif
int32_t pal_socket_backend_get_local_address(
    void *state,
    pal_socket_address_t *address)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;
    SOCKADDR_STORAGE native;
    int native_len = (int)sizeof(native);

    if (socket_state == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (getsockname(
            socket_state->descriptor,
            (SOCKADDR *)&native,
            &native_len) == SOCKET_ERROR)
    {
        return pal_windows_socket_error(WSAGetLastError());
    }
    return pal_windows_from_sockaddr(&native, native_len, address);
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;
    struct sockaddr_storage native;
    socklen_t native_len = (socklen_t)sizeof(native);

    if (socket_state == NULL || address == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (getsockname(
            socket_state->descriptor,
            (struct sockaddr *)&native,
            &native_len) != 0)
    {
        return pal_linux_socket_error(errno);
    }
    return pal_linux_from_sockaddr(&native, native_len, address);
#endif
}

#if defined(_WIN32)
/**
 * @brief      Shuts down one or both directions of a Windows TCP socket.
 *
 * @details    Maps the PAL shutdown mode to SD_RECEIVE, SD_SEND, or SD_BOTH
 *             and invokes shutdown().
 *
 * @param[in]  state  Connected TCP backend state.
 * @param[in]  mode   Directions to disable.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   shutdown() succeeded.
 * @retval     PAL_ERR_NULL_POINTER     Backend state is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not TCP or mode is invalid.
 * @retval     PAL_ERR_NOT_CONNECTED    Socket is not connected.
 * @retval     PAL_ERR_IO               shutdown() failed.
 *
 * @note       Shutdown does not close the socket.
 * @warning    A shutdown direction cannot be re-enabled.
 */
#elif defined(__linux__)
/**
 * @brief      Shuts down one or both directions of a Linux TCP socket.
 *
 * @details    Maps the PAL shutdown mode to SHUT_RD, SHUT_WR, or SHUT_RDWR and
 *             calls shutdown().
 *
 * @param[in]  state  Connected TCP backend state.
 * @param[in]  mode   Directions to disable.
 *
 * @return     PAL_OK or a negative PAL error code.
 * @retval     PAL_OK                   shutdown() succeeded.
 * @retval     PAL_ERR_NULL_POINTER     Backend state is NULL.
 * @retval     PAL_ERR_INVALID_ARGUMENT Socket is not TCP or mode is invalid.
 * @retval     PAL_ERR_NOT_CONNECTED    Socket is not connected.
 * @retval     PAL_ERR_IO               shutdown() failed.
 *
 * @note       Shutdown does not close the descriptor.
 * @warning    A shutdown direction cannot be re-enabled.
 */
#endif
int32_t pal_socket_backend_shutdown(void *state, pal_socket_shutdown_mode_t mode)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;
    int native_mode;

    if (socket_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket_state->type != PAL_SOCKET_TYPE_TCP)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    if (!socket_state->connected)
    {
        return PAL_ERR_NOT_CONNECTED;
    }
    native_mode = (mode == PAL_SOCKET_SHUTDOWN_READ)
        ? SD_RECEIVE
        : (mode == PAL_SOCKET_SHUTDOWN_WRITE)
            ? SD_SEND
            : (mode == PAL_SOCKET_SHUTDOWN_BOTH) ? SD_BOTH : -1;
    if (native_mode < 0)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    return (shutdown(socket_state->descriptor, native_mode) == 0)
        ? PAL_OK
        : pal_windows_socket_error(WSAGetLastError());
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;
    int native_mode;

    if (socket_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (socket_state->type != PAL_SOCKET_TYPE_TCP)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    if (!socket_state->connected)
    {
        return PAL_ERR_NOT_CONNECTED;
    }

    native_mode = (mode == PAL_SOCKET_SHUTDOWN_READ)
        ? SHUT_RD
        : (mode == PAL_SOCKET_SHUTDOWN_WRITE)
            ? SHUT_WR
            : (mode == PAL_SOCKET_SHUTDOWN_BOTH) ? SHUT_RDWR : -1;
    if (native_mode < 0)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    return (shutdown(socket_state->descriptor, native_mode) == 0)
        ? PAL_OK
        : pal_linux_socket_error(errno);
#endif
}

#if defined(_WIN32)
/**
 * @brief      Closes and frees a Windows socket backend state.
 *
 * @details    Calls closesocket() and releases the backend allocation only
 *             after native closure succeeds.
 *
 * @param[in]  state  Windows socket backend state to close.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Socket was closed.
 * @retval     PAL_ERR_NULL_POINTER   Backend state is NULL.
 * @retval     PAL_ERR_IO             closesocket() failed.
 *
 * @note       Winsock remains initialized for process lifetime.
 * @warning    Backend state is invalid after successful closure.
 */
#elif defined(__linux__)
/**
 * @brief      Closes and frees a Linux socket backend state.
 *
 * @details    Closes the native descriptor and frees the backend allocation
 *             only when close() succeeds.
 *
 * @param[in]  state  Linux socket backend state to close.
 *
 * @return     PAL_OK on success or a negative PAL error code.
 * @retval     PAL_OK                 Socket descriptor was closed.
 * @retval     PAL_ERR_NULL_POINTER   Backend state is NULL.
 * @retval     PAL_ERR_IO             close() failed.
 *
 * @note       close() releases the descriptor from the process.
 * @warning    The backend state is invalid after successful closure.
 */
#endif
int32_t pal_socket_backend_close(void *state)
{
#if defined(_WIN32)
    pal_windows_socket_state_t *socket_state = (pal_windows_socket_state_t *)state;

    if (socket_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (closesocket(socket_state->descriptor) == SOCKET_ERROR)
    {
        return pal_windows_socket_error(WSAGetLastError());
    }
    free(socket_state);
    return PAL_OK;
#elif defined(__linux__)
    pal_linux_socket_state_t *socket_state = (pal_linux_socket_state_t *)state;

    if (socket_state == NULL)
    {
        return PAL_ERR_NULL_POINTER;
    }
    if (close(socket_state->descriptor) != 0)
    {
        return pal_linux_socket_error(errno);
    }
    free(socket_state);
    return PAL_OK;
#endif
}
