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
compiles the unified backend sources under `src/platform/`, where preprocessor
conditions select Windows or Linux code. The Windows build links against
Winsock (`ws2_32`); the Linux build links against the platform thread library.

## Use PAL from another CMake project

This project currently builds a static library named `pal_base`. Add this
directory to a parent CMake project and link the library target:

```cmake
add_subdirectory(path/to/pal_library)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE pal_base)
```

The `inc/` directory is exported as a public include directory, so application
code includes public headers by their file name, for example:

```c
#include "pal_error.h"
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
| `pal_mutex.h` | Mutex lifecycle, lock, and unlock |
| `pal_queue.h` | Bounded FIFO queue storing caller-owned pointers |
| `pal_thread.h` | Thread lifecycle and thread return values |
| `pal_socket.h` | IPv4/IPv6 TCP and UDP socket operations |
| `pal_time.h` | Monotonic/UTC clocks, sleep, stopwatch, and deadline |

All PAL functions that return a status use `PAL_OK` for success and a negative
`PAL_ERR_*` value for failure. Use `pal_error_to_string()` to get a readable
status name. Native OS error values are not returned directly.

Timeouts passed to queue and socket operations are in milliseconds. Use
`PAL_NO_WAIT` for a non-blocking attempt and `PAL_WAIT_FOREVER` for an indefinite
wait. Time API sleep durations are explicitly named by unit (`_ns`, `_us`, or
`_ms`); monotonic time is intended for elapsed-time measurements, while UTC
time is a wall-clock timestamp.

Queue entries are pointer values: the queue does not copy or manage the memory
referenced by an entry. Callers remain responsible for the lifetime of those
objects.

## Project layout

```text
inc/                 Public API headers and private pal_backend.h interface
src/common/          Platform-independent implementations
src/platform/        Platform-selected backend implementations
test_pal_base.c      Self-checking test executable
CMakeLists.txt       Build and CTest configuration
```

## Tests and console output

The test executable prints each test function, describes the operation under
test, and reports whether each step passed or failed. Failed checks include the
source file, line, and failed expression. The final line reports the total
number of checks and failures. A nonzero process exit code indicates failure.

## Extending platform support

Common API implementations are separated from operating-system backends.
Adding another target requires implementing the private backend interfaces in
`inc/pal_backend.h` and adding the new platform implementation and build
configuration. RTOS and bare-metal targets are not currently implemented.
