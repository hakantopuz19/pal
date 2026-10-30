# Platform Abstraction Layer (PAL)

PAL is a C99 library that provides a common API for platform services. The
current backends support Linux and Windows; CMake selects the implementation
for the configured target automatically.

## Features

- Platform and compiler identification
- Common PAL status and error codes
- Mutexes and bounded FIFO queues
- Thread creation, joining, detaching, and exit
- IPv4/IPv6 TCP and UDP sockets with timeout-based I/O
- A simplified connection layer for TCP/UDP clients and servers
- Monotonic and UTC clocks, sleep, stopwatches, and deadlines
- A self-checking test executable with per-test and per-step console results

## Requirements

- CMake 3.10 or newer
- A C99-compatible compiler
- Linux: POSIX threads (usually provided by the system C library and toolchain)
- Windows: Windows Vista or newer and a Windows C compiler

The project uses strict warnings and treats warnings as errors:

- GCC/Clang: `-Wall -Wextra -Wpedantic -Werror`
- MSVC: `/W4 /WX`

## Build and run on Linux

From the project root:

```sh
cmake -S . -B build
cmake --build build --parallel
```

Run the test executable directly to see the detailed console log:

```sh
./build/test_pal_base
```

Or run it through CTest:

```sh
ctest --test-dir build --output-on-failure
```

## Build and run on Windows

From PowerShell in the project root, configure with a generator installed on
your machine. For example, with Visual Studio:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run the test executable:

```powershell
.\build\Release\test_pal_base.exe
```

CTest can also run the test:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

No platform-specific source path needs to be selected by the user. CMake
compiles the unified backend sources under `pal_library/src/platform/`, where
preprocessor conditions select Windows or Linux code. The Windows build links
against Winsock (`ws2_32`); the Linux build links against the platform thread
library.

Each library can also be configured independently. Configuring
`pal_connection/` builds and links its `pal_base` dependency automatically:

```sh
cmake -S pal_library -B build-pal
cmake --build build-pal --parallel

cmake -S pal_connection -B build-connection
cmake --build build-connection --parallel
```

## Use PAL from another CMake project

The root CMake project builds two static libraries in separate directories:
`pal_library/` contains the `pal_base` core platform APIs, and
`pal_connection/` contains autonomous TCP/UDP services. `pal_connection` links
to `pal_base`. Add the repository root to a parent CMake project and link the
target you need:

```cmake
add_subdirectory(path/to/pal_library)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE pal_connection)
```

Link `pal_base` instead if the application does not use connection services.
Both libraries export their own `inc/` directory as a public include directory,
so application code includes public headers by file name, for example:

```c
#include "pal_error.h"
#include "pal_connection.h"
#include "pal_mutex.h"
#include "pal_time.h"
```

## Quick example

The following C99 program creates a mutex, reads monotonic time, and reports
PAL errors by their symbolic name:

```c
#include <inttypes.h>
#include <stdio.h>

#include "pal_error.h"
#include "pal_mutex.h"
#include "pal_time.h"

int main(void)
{
    pal_mutex_t *mutex = NULL;
    uint64_t time_ns = 0u;
    int32_t status;

    status = pal_mutex_create(&mutex);
    if (status != PAL_OK)
    {
        fprintf(stderr, "pal_mutex_create failed: %s\n",
                pal_error_to_string(status));
        return 1;
    }

    status = pal_mutex_lock(mutex);
    if (status == PAL_OK)
    {
        status = pal_time_monotonic_ns(&time_ns);
        if (status == PAL_OK)
        {
            printf("Monotonic time: %" PRIu64 " ns\n", time_ns);
        }

        if (pal_mutex_unlock(mutex) != PAL_OK)
        {
            fprintf(stderr, "pal_mutex_unlock failed\n");
            (void)pal_mutex_destroy(mutex);
            return 1;
        }
    }

    if (pal_mutex_destroy(mutex) != PAL_OK)
    {
        fprintf(stderr, "pal_mutex_destroy failed\n");
        return 1;
    }

    if (status != PAL_OK)
    {
        fprintf(stderr, "PAL operation failed: %s\n",
                pal_error_to_string(status));
        return 1;
    }

    return 0;
}
```

If the example is part of the parent project above, link `my_app` to `pal_base`;
the include path and platform-specific link dependencies are then propagated
by CMake.

## Public API overview

| Header | Functionality |
|---|---|
| `pal_types.h` | Common types, timeout constants, helper macros, and version |
| `pal_error.h` | PAL status codes and status-to-name conversion |
| `pal_platform.h` | Compile-time and runtime platform information |
| `pal_mutex.h` | Mutex lifecycle, lock, unlock, and the process-wide PAL mutex |
| `pal_queue.h` | Bounded FIFO queue storing caller-owned pointers |
| `pal_thread.h` | Thread lifecycle and thread return values |
| `pal_socket.h` | IPv4/IPv6 TCP and UDP socket operations |
| `pal_connection.h` | Simplified TCP/UDP client and server operations |
| `pal_time.h` | Monotonic/UTC clocks, sleep, stopwatch, and deadline |

All PAL functions that return a status use `PAL_OK` for success and a negative
`PAL_ERR_*` value for failure. Use `pal_error_to_string()` to get a readable
status name. Native OS error values are not returned directly.

The process-wide mutex functions in `pal_mutex.h` are intended for static
shared state that has no object-specific mutex. They are non-recursive and
must always be paired; avoid calling code that may acquire the same global
mutex while holding it.

Timeouts passed to queue and socket operations are in milliseconds. Use
`PAL_NO_WAIT` for a non-blocking attempt and `PAL_WAIT_FOREVER` for an indefinite
wait. Time API sleep durations are explicitly named by unit (`_ns`, `_us`, or
`_ms`); monotonic time is intended for elapsed-time measurements, while UTC
time is a wall-clock timestamp.

Queue entries are pointer values: the queue does not copy or manage the memory
referenced by an entry. Callers remain responsible for the lifetime of those
objects.

### Connection layer

`pal_connection.h` is a separate `pal_connection` library target layered on
`pal_base`. Opening a connection starts background receive processing
automatically:

- `pal_connection_open_server()` starts a TCP accept worker or UDP receive
  worker.
- `pal_connection_open_client()` starts a TCP/UDP receive worker after client
  setup.
- TCP servers handle multiple clients concurrently, with one worker per
  accepted client.
- Received data is delivered to the optional callback and is also copied to a
  bounded event queue. Read queued events with `pal_connection_next_event()`
  and release each one with `pal_connection_event_release()`.
- `pal_connection_get_worker_status()` reports the first unexpected error
  reported by a background worker; `PAL_OK` means no worker error was recorded.
- TCP callbacks and queued events include a `peer_id`; pass it to
  `pal_connection_send()` to reply to that active TCP client. Client sends use
  peer ID zero.
- UDP clients send to their configured destination. UDP servers automatically
  direct sends to the most recently received datagram's sender.
- `pal_connection_close()` explicitly stops workers and frees their sockets,
  queues, and synchronization objects.
- Any still-open services are also closed when the program exits normally
  (return from `main()` or `exit()`). This cleanup does not run after `abort()`,
  `_Exit()`, or abnormal process termination.
- The first connection API use creates or opens `logs.db` in the process's
  current working directory. Connection operations, incoming and outgoing
  payloads, result statuses, and peer metadata are appended to the
  `connection_logs` SQLite table. Payloads are stored as raw BLOB values.
- The table columns are `id`, `timestamp_utc`, `operation`, `direction`,
  `transport`, `role`, `peer_id`, `status`, `payload_size`, and `payload`.
  Incoming records use direction `in`; outgoing records use `out`. Payloads
  contain the exact bytes received or sent successfully.
- SQLite 3.54.1 is included in `pal_connection/third_party/sqlite` and built
  into the connection library; no system SQLite development package is needed.
  If opening or writing the log database fails, the error is reported to
  `stderr` and connection processing continues.

The connection API uses `pal_socket_address_t` from `pal_socket.h`, so callers
provide an address family, address bytes, and port. Set server address bytes to
zero to bind on all interfaces; set port to zero to request an OS-assigned port. TCP client
connection establishment uses an infinite timeout. Send and event-poll
timeouts are in milliseconds. The event queue holds 64 events;
when full, the oldest queued event is discarded. Query the cumulative drop
count with `pal_connection_get_dropped_event_count()`.

UDP clients send to the peer configured when opened. UDP servers remember the
most recent successful datagram sender and direct subsequent sends to that
peer. Therefore the convenience reply behavior is intended for simple
request/reply flows; concurrent receives and sends on one UDP server endpoint
are not synchronized. TCP callbacks receive arbitrary stream chunks rather
than application messages; implement message framing in the application.
Callbacks run on worker threads and should return promptly. TCP server callbacks
for different peers can run concurrently, so callback user data must be
thread-safe. Do not call `pal_connection_close()` for the same service from its
callback; close joins the worker, and a callback must return before shutdown can
finish.

### TCP echo example

This complete example can run as either a server or a client. It demonstrates
creating the socket address, starting a background server, sending and
receiving data, checking errors, and closing the service. Save it as
`connection_example.c` in a project that links to `pal_connection`:

```c
#include <stdio.h>
#include <string.h>

#include "pal_connection.h"
#include "pal_error.h"
#include "pal_socket.h"

static void echo_received_data(
    pal_connection_t *connection,
    const pal_connection_event_t *event,
    void *user_data)
{
    size_t sent = 0u;
    int32_t status;

    (void)user_data;
    status = pal_connection_send(
        connection,
        event->peer_id,
        event->data,
        event->size,
        &sent,
        1000u);
    if (status != PAL_OK)
    {
        fprintf(stderr, "Echo send failed: %s\n", pal_error_to_string(status));
    }
    else if (sent != event->size)
    {
        fprintf(stderr, "Echo send was incomplete (%zu of %zu bytes)\n",
                sent, event->size);
    }
}

static void set_loopback_address(pal_socket_address_t *address, uint16_t port)
{
    memset(address, 0, sizeof(*address));
    address->family = PAL_SOCKET_FAMILY_IPV4;
    address->address[0] = 127u;
    address->address[3] = 1u;
    address->port = port;
}

static int run_server(void)
{
    pal_connection_t *server = NULL;
    pal_socket_address_t address;
    int32_t status;

    set_loopback_address(&address, 9000u);
    status = pal_connection_open_server(
        PAL_CONNECTION_TRANSPORT_TCP,
        &address,
        echo_received_data,
        NULL,
        &server);
    if (status != PAL_OK)
    {
        fprintf(stderr, "Cannot start server: %s\n", pal_error_to_string(status));
        return 1;
    }

    printf("TCP echo server listening on 127.0.0.1:9000. Press Enter to stop.\n");
    (void)getchar();

    status = pal_connection_close(server);
    if (status != PAL_OK)
    {
        fprintf(stderr, "Cannot close server: %s\n", pal_error_to_string(status));
        return 1;
    }
    return 0;
}

static int run_client(void)
{
    static const char message[] = "Hello from pal_connection";
    pal_connection_t *client = NULL;
    pal_connection_event_t *event = NULL;
    pal_socket_address_t address;
    size_t sent = 0u;
    int32_t status;
    int result = 1;

    set_loopback_address(&address, 9000u);
    status = pal_connection_open_client(
        PAL_CONNECTION_TRANSPORT_TCP,
        &address,
        NULL,
        NULL,
        &client);
    if (status != PAL_OK)
    {
        fprintf(stderr, "Cannot connect to server: %s\n", pal_error_to_string(status));
        return 1;
    }

    status = pal_connection_send(
        client, 0u, message, sizeof(message) - 1u, &sent, 1000u);
    if (status != PAL_OK)
    {
        fprintf(stderr, "Send failed: %s\n", pal_error_to_string(status));
        goto cleanup;
    }
    if (sent != sizeof(message) - 1u)
    {
        fprintf(stderr, "Send was incomplete (%zu of %zu bytes)\n",
                sent, sizeof(message) - 1u);
        goto cleanup;
    }

    status = pal_connection_next_event(client, 5000u, &event);
    if (status != PAL_OK)
    {
        fprintf(stderr, "Receive failed: %s\n", pal_error_to_string(status));
        goto cleanup;
    }

    printf("Server replied: ");
    (void)fwrite(event->data, 1u, event->size, stdout);
    putchar('\n');
    result = 0;

cleanup:
    pal_connection_event_release(event);
    status = pal_connection_close(client);
    if (status != PAL_OK)
    {
        fprintf(stderr, "Cannot close client: %s\n", pal_error_to_string(status));
        result = 1;
    }
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s server|client\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "server") == 0)
    {
        return run_server();
    }
    if (strcmp(argv[1], "client") == 0)
    {
        return run_client();
    }
    fprintf(stderr, "Unknown mode: %s\n", argv[1]);
    return 1;
}
```

In the consuming CMake project, link the example executable to the connection
target:

```cmake
add_executable(connection_example connection_example.c)
target_link_libraries(connection_example PRIVATE pal_connection)
```

Build and run the server and client in separate terminals:

```sh
cmake -S . -B build
cmake --build build --parallel
./build/connection_example server
```

In a second terminal, from the same working directory:

```sh
./build/connection_example client
```

The client should print the server's reply. Press Enter in the server terminal
to stop it cleanly. The library creates or reuses `logs.db` in the working
directory; the example records connection operations and raw payload bytes
there. The TCP server binds only to IPv4 loopback; use an address with zeroed
address bytes to listen on all local interfaces instead.

The callback must return promptly and must not close its own connection.
TCP callbacks receive arbitrary stream chunks, not guaranteed application
messages; this example sends one small payload, but production protocols
should implement framing. The server callback uses `event->peer_id` to send
the reply to the correct TCP client.

For applications that prefer polling instead of callbacks, pass `NULL` for the
callback when opening a service and use `pal_connection_next_event()` to wait
for received data. Release every successfully returned event with
`pal_connection_event_release()`.

Open connection services are registered for normal process-exit cleanup.
Returning from `main()` or calling `exit()` stops workers, joins threads,
closes sockets, and releases internal synchronization resources. This cleanup
cannot run after `abort()`, `_Exit()`, or abrupt process termination. Callback
functions must return promptly and must not close their own connection.

The database contains raw application traffic, which may include credentials,
tokens, personal information, or other sensitive values. Protect `logs.db` with
appropriate filesystem permissions and remove it when it is no longer needed.

## Project layout

```text
pal_library/
  inc/               PAL base public API and private backend interface
  src/common/        Platform-independent implementations
  src/platform/      Platform-selected backend implementations
  CMakeLists.txt     pal_base target
pal_connection/
  inc/               Connection API and logger interface headers
  src/               Connection implementation
  third_party/sqlite/ Vendored SQLite 3 amalgamation
  CMakeLists.txt     pal_connection target
test_pal_base.c      Self-checking test executable
CMakeLists.txt       Top-level build and CTest configuration
```

## Tests and console output

The test executable prints each test function, describes the operation under
test, and reports whether each step passed or failed. Failed checks include the
source file, line, and failed expression. The final line reports the total
number of checks and failures. A nonzero process exit code indicates failure.

## Extending platform support

Common API implementations are separated from operating-system backends.
Adding another target requires implementing the private backend interfaces in
`pal_library/inc/pal_backend.h` and adding the new platform implementation and
build configuration. RTOS and bare-metal targets are not currently
implemented.
