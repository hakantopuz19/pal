# Build description of the PAL base library and its tests.
cmake_minimum_required(VERSION 3.10)
project(pal_base C)

set(PAL_PLATFORM_BACKENDS
    src/platform/pal_sync_backend.c
    src/platform/pal_thread_backend.c
    src/platform/pal_socket_backend.c
    src/platform/pal_time_backend.c
)

if(WIN32)
    # The unified backend sources select their Windows implementation via _WIN32.
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The unified backend sources select their Linux implementation via __linux__.
    find_package(Threads REQUIRED)
else()
    message(FATAL_ERROR "PAL socket, thread, synchronization, and time backends are available for Linux and Windows.")
endif()

add_library(pal_base STATIC
    inc/pal_backend.h
    src/common/pal_error.c
    src/common/pal_platform.c
    src/common/pal_mutex.c
    src/common/pal_queue.c
    src/common/pal_thread.c
    src/common/pal_socket.c
    src/common/pal_time.c
    ${PAL_PLATFORM_BACKENDS}
)

target_include_directories(pal_base PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

if(UNIX)
    target_link_libraries(pal_base PRIVATE Threads::Threads)
elseif(WIN32)
    target_link_libraries(pal_base PRIVATE ws2_32)
endif()

set_target_properties(pal_base PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

add_executable(test_pal_base test_pal_base.c)
target_link_libraries(test_pal_base PRIVATE pal_base)
set_target_properties(test_pal_base PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(pal_base PRIVATE /W4 /WX)
    target_compile_options(test_pal_base PRIVATE /W4 /WX)
else()
    target_compile_options(pal_base PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(test_pal_base PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME pal_base COMMAND test_pal_base)
