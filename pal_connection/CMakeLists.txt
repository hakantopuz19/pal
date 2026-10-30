cmake_minimum_required(VERSION 3.10)
project(pal_connection C)

if(NOT TARGET pal_base)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../pal_library"
        "${CMAKE_CURRENT_BINARY_DIR}/pal_library"
    )
endif()

add_library(pal_connection STATIC
    src/pal_connection.c
    src/pal_connection_log.c
)

add_library(pal_connection_sqlite STATIC
    third_party/sqlite/sqlite3.c
)

target_include_directories(pal_connection_sqlite PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)

target_include_directories(pal_connection PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_include_directories(pal_connection PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)

target_link_libraries(pal_connection PUBLIC pal_base PRIVATE pal_connection_sqlite)

set_target_properties(pal_connection PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

set_target_properties(pal_connection_sqlite PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    target_link_libraries(pal_connection_sqlite PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

if(MSVC)
    target_compile_options(pal_connection PRIVATE /W4 /WX)
else()
    target_compile_options(pal_connection PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
