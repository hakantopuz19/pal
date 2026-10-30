/**
 * @file    pal_types.h
 * @brief   Fundamental platform independent types and helper macros of the PAL.
 *
 * @details This header is intentionally self contained. By project rule it
 *          includes ONLY standard C library headers and never another PAL
 *          header, so it can be used on its own on any target (Windows, Linux,
 *          RTOS, bare metal).
 *
 *          Naming policy (collision avoidance):
 *          - every macro and enumerator starts with  PAL_
 *          - every type and function starts with     pal_
 *          No name here is ever re-used from a system header.
 */

#ifndef PAL_TYPES_H
#define PAL_TYPES_H

#include <stddef.h>   /* size_t, NULL                                   */
#include <stdint.h>   /* fixed width integers, uintptr_t, UINT32_MAX    */
#include <stdbool.h>  /* bool, true, false                              */

/** Major version of the PAL base library (incompatible API changes). */
#define PAL_VERSION_MAJOR 0
/** Minor version of the PAL base library (backward compatible features). */
#define PAL_VERSION_MINOR 3
/** Patch version of the PAL base library (backward compatible fixes). */
#define PAL_VERSION_PATCH 0

/**
 * @brief Opaque, pointer sized handle for any OS or hardware resource.
 *
 * @details Backends store a native object here (file descriptor, HANDLE,
 *          RTOS control block pointer, ...). Users must never interpret it.
 */
typedef uintptr_t pal_handle_t;

/** Value of a handle that does not refer to any resource. */
#define PAL_INVALID_HANDLE ((pal_handle_t)0)

/** Timeout expressed in milliseconds. */
typedef uint32_t pal_timeout_t;

/** Timeout value meaning "do not wait at all" (poll). */
#define PAL_NO_WAIT ((pal_timeout_t)0u)

/** Timeout value meaning "wait without any time limit". */
#define PAL_WAIT_FOREVER ((pal_timeout_t)UINT32_MAX)

/** Marks a parameter or variable as intentionally unused (silences warnings). */
#define PAL_UNUSED(x) ((void)(x))

/**
 * @brief Number of elements of a real array (NOT of a pointer).
 * @note  The argument is not evaluated, only its size is inspected.
 */
#define PAL_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

/**
 * @brief Compile time assertion usable at file or block scope.
 *
 * @details @p msg must be a valid C identifier (not a string) so the macro
 *          also works for pre C11 compilers through the negative array size
 *          trick. Example: PAL_STATIC_ASSERT(sizeof(int) >= 2, int_is_wide);
 */
#if defined(__cplusplus)
    #define PAL_STATIC_ASSERT(cond, msg) static_assert((cond), #msg)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define PAL_STATIC_ASSERT(cond, msg) _Static_assert((cond), #msg)
#else
    #define PAL_STATIC_ASSERT(cond, msg) \
        typedef char pal_static_assert_##msg[(cond) ? 1 : -1]
#endif

#endif /* PAL_TYPES_H */
