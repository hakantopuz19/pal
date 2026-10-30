cmake_minimum_required(VERSION 3.10)
project(pal_base C)

add_library(pal_base STATIC
    inc/pal_backend.h
    src/common/pal_error.c
    src/common/pal_platform.c
    src/common/pal_mutex.c
    src/common/pal_queue.c
    src/common/pal_thread.c
    src/common/pal_socket.c
    src/common/pal_time.c
    src/platform/pal_sync_backend.c
    src/platform/pal_thread_backend.c
    src/platform/pal_socket_backend.c
    src/platform/pal_time_backend.c
)

target_include_directories(pal_base PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    target_link_libraries(pal_base PRIVATE Threads::Threads)
elseif(WIN32)
    target_link_libraries(pal_base PRIVATE ws2_32)
else()
    message(FATAL_ERROR "PAL backends are available for Linux and Windows.")
endif()

set_target_properties(pal_base PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(pal_base PRIVATE /W4 /WX)
else()
    target_compile_options(pal_base PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
