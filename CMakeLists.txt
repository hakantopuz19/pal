cmake_minimum_required(VERSION 3.10)
project(pal_library C)

add_subdirectory(pal_library)
add_subdirectory(pal_connection)

add_executable(test_pal_base test_pal_base.c)
target_link_libraries(test_pal_base PRIVATE pal_connection pal_connection_sqlite)
target_include_directories(test_pal_base PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pal_connection/third_party/sqlite
)
set_target_properties(test_pal_base PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(test_pal_base PRIVATE /W4 /WX)
else()
    target_compile_options(test_pal_base PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME pal_base COMMAND test_pal_base)
