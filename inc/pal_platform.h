/**
 * @file    pal_platform.h
 * @brief   Compile time detection of operating system, compiler, CPU
 *          architecture, byte order and pointer width.
 *
 * @details Self contained header (standard includes only).
 *
 *          Every detection result is exposed as a macro that is always
 *          defined and evaluates to 0 or 1, so both  "#if PAL_OS_LINUX"  and
 *          "if (PAL_OS_LINUX)"  are valid and typos are not silently false.
 *
 *          RTOS and bare metal targets cannot be detected reliably from
 *          compiler macros. Select them explicitly from the build system:
 *              -DPAL_TARGET_RTOS        or    -DPAL_TARGET_BAREMETAL
 *          These override automatic detection. Byte order can be forced with
 *              -DPAL_FORCE_LITTLE_ENDIAN   or   -DPAL_FORCE_BIG_ENDIAN
 */

#ifndef PAL_PLATFORM_H
#define PAL_PLATFORM_H

#include <stdint.h>   /* UINTPTR_MAX */
#include <stdbool.h>  /* bool        */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Operating system                                                    */
/* ------------------------------------------------------------------ */

#define PAL_OS_ID_WINDOWS   1   /**< Microsoft Windows.                */
#define PAL_OS_ID_LINUX     2   /**< Linux (including Android).        */
#define PAL_OS_ID_MACOS     3   /**< Apple macOS.                      */
#define PAL_OS_ID_RTOS      4   /**< Any RTOS (explicit selection).    */
#define PAL_OS_ID_BAREMETAL 5   /**< No OS at all (explicit selection).*/

#if defined(PAL_TARGET_BAREMETAL) && defined(PAL_TARGET_RTOS)
    #error "PAL: define only one of PAL_TARGET_BAREMETAL or PAL_TARGET_RTOS."
#elif defined(PAL_TARGET_BAREMETAL)
    #define PAL_OS_ID PAL_OS_ID_BAREMETAL
#elif defined(PAL_TARGET_RTOS)
    #define PAL_OS_ID PAL_OS_ID_RTOS
#elif defined(_WIN32)
    #define PAL_OS_ID PAL_OS_ID_WINDOWS
#elif defined(__linux__)
    #define PAL_OS_ID PAL_OS_ID_LINUX
#elif defined(__APPLE__) && defined(__MACH__)
    #define PAL_OS_ID PAL_OS_ID_MACOS
#else
    #error "PAL: unknown target OS. Define PAL_TARGET_RTOS or PAL_TARGET_BAREMETAL."
#endif

#define PAL_OS_WINDOWS   (PAL_OS_ID == PAL_OS_ID_WINDOWS)    /**< 1 on Windows.    */
#define PAL_OS_LINUX     (PAL_OS_ID == PAL_OS_ID_LINUX)      /**< 1 on Linux.      */
#define PAL_OS_MACOS     (PAL_OS_ID == PAL_OS_ID_MACOS)      /**< 1 on macOS.      */
#define PAL_OS_RTOS      (PAL_OS_ID == PAL_OS_ID_RTOS)       /**< 1 on an RTOS.    */
#define PAL_OS_BAREMETAL (PAL_OS_ID == PAL_OS_ID_BAREMETAL)  /**< 1 on bare metal. */
#define PAL_OS_POSIX     (PAL_OS_LINUX || PAL_OS_MACOS)      /**< 1 on POSIX like. */

/* ------------------------------------------------------------------ */
/* Compiler                                                            */
/* ------------------------------------------------------------------ */

#define PAL_COMPILER_ID_UNKNOWN 0   /**< Unrecognized compiler. */
#define PAL_COMPILER_ID_MSVC    1   /**< Microsoft Visual C.    */
#define PAL_COMPILER_ID_GCC     2   /**< GNU GCC.               */
#define PAL_COMPILER_ID_CLANG   3   /**< LLVM Clang.            */

/* Clang also defines __GNUC__, so it must be tested first. */
#if defined(__clang__)
    #define PAL_COMPILER_ID PAL_COMPILER_ID_CLANG
#elif defined(__GNUC__)
    #define PAL_COMPILER_ID PAL_COMPILER_ID_GCC
#elif defined(_MSC_VER)
    #define PAL_COMPILER_ID PAL_COMPILER_ID_MSVC
#else
    #define PAL_COMPILER_ID PAL_COMPILER_ID_UNKNOWN
#endif

#define PAL_COMPILER_UNKNOWN (PAL_COMPILER_ID == PAL_COMPILER_ID_UNKNOWN) /**< 1 if unknown. */
#define PAL_COMPILER_MSVC    (PAL_COMPILER_ID == PAL_COMPILER_ID_MSVC)    /**< 1 on MSVC.    */
#define PAL_COMPILER_GCC     (PAL_COMPILER_ID == PAL_COMPILER_ID_GCC)     /**< 1 on GCC.     */
#define PAL_COMPILER_CLANG   (PAL_COMPILER_ID == PAL_COMPILER_ID_CLANG)   /**< 1 on Clang.   */

/* ------------------------------------------------------------------ */
/* CPU architecture                                                    */
/* ------------------------------------------------------------------ */

#define PAL_ARCH_ID_UNKNOWN 0   /**< Unrecognized architecture. */
#define PAL_ARCH_ID_X86     1   /**< 32 bit x86.                */
#define PAL_ARCH_ID_X64     2   /**< 64 bit x86 (AMD64).        */
#define PAL_ARCH_ID_ARM     3   /**< 32 bit ARM.                */
#define PAL_ARCH_ID_ARM64   4   /**< 64 bit ARM (AArch64).      */
#define PAL_ARCH_ID_RISCV   5   /**< RISC-V.                    */

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    #define PAL_ARCH_ID PAL_ARCH_ID_X64
#elif defined(__i386__) || defined(_M_IX86)
    #define PAL_ARCH_ID PAL_ARCH_ID_X86
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define PAL_ARCH_ID PAL_ARCH_ID_ARM64
#elif defined(__arm__) || defined(_M_ARM)
    #define PAL_ARCH_ID PAL_ARCH_ID_ARM
#elif defined(__riscv)
    #define PAL_ARCH_ID PAL_ARCH_ID_RISCV
#else
    #define PAL_ARCH_ID PAL_ARCH_ID_UNKNOWN
#endif

#define PAL_ARCH_UNKNOWN (PAL_ARCH_ID == PAL_ARCH_ID_UNKNOWN) /**< 1 if unknown. */
#define PAL_ARCH_X86     (PAL_ARCH_ID == PAL_ARCH_ID_X86)     /**< 1 on x86.     */
#define PAL_ARCH_X64     (PAL_ARCH_ID == PAL_ARCH_ID_X64)     /**< 1 on x64.     */
#define PAL_ARCH_ARM     (PAL_ARCH_ID == PAL_ARCH_ID_ARM)     /**< 1 on ARM.     */
#define PAL_ARCH_ARM64   (PAL_ARCH_ID == PAL_ARCH_ID_ARM64)   /**< 1 on ARM64.   */
#define PAL_ARCH_RISCV   (PAL_ARCH_ID == PAL_ARCH_ID_RISCV)   /**< 1 on RISC-V.  */

/* ------------------------------------------------------------------ */
/* Byte order                                                          */
/* ------------------------------------------------------------------ */

#define PAL_BYTE_ORDER_LITTLE 1   /**< Least significant byte first. */
#define PAL_BYTE_ORDER_BIG    2   /**< Most significant byte first.  */

#if defined(PAL_FORCE_LITTLE_ENDIAN) && defined(PAL_FORCE_BIG_ENDIAN)
    #error "PAL: define only one of PAL_FORCE_LITTLE_ENDIAN or PAL_FORCE_BIG_ENDIAN."
#elif defined(PAL_FORCE_LITTLE_ENDIAN)
    #define PAL_BYTE_ORDER PAL_BYTE_ORDER_LITTLE
#elif defined(PAL_FORCE_BIG_ENDIAN)
    #define PAL_BYTE_ORDER PAL_BYTE_ORDER_BIG
#elif defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
      (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    #define PAL_BYTE_ORDER PAL_BYTE_ORDER_LITTLE
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
      (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    #define PAL_BYTE_ORDER PAL_BYTE_ORDER_BIG
#elif defined(_WIN32)
    /* All Windows targets are little endian. */
    #define PAL_BYTE_ORDER PAL_BYTE_ORDER_LITTLE
#else
    #error "PAL: cannot detect byte order. Define PAL_FORCE_LITTLE_ENDIAN or PAL_FORCE_BIG_ENDIAN."
#endif

#define PAL_IS_LITTLE_ENDIAN (PAL_BYTE_ORDER == PAL_BYTE_ORDER_LITTLE) /**< 1 if little endian. */
#define PAL_IS_BIG_ENDIAN    (PAL_BYTE_ORDER == PAL_BYTE_ORDER_BIG)    /**< 1 if big endian.    */

/* ------------------------------------------------------------------ */
/* Pointer width                                                       */
/* ------------------------------------------------------------------ */

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
    #define PAL_PTR_BITS 64   /**< Pointer width in bits. */
#elif UINTPTR_MAX == 0xFFFFFFFFu
    #define PAL_PTR_BITS 32   /**< Pointer width in bits. */
#elif UINTPTR_MAX == 0xFFFFu
    #define PAL_PTR_BITS 16   /**< Pointer width in bits. */
#else
    #error "PAL: unsupported pointer width."
#endif

/* ------------------------------------------------------------------ */
/* Runtime queries (full documentation: src/common/pal_platform.c)      */
/* ------------------------------------------------------------------ */

/* Human readable name of the target OS, never NULL. */
const char *pal_platform_get_os_name(void);

/* Human readable name of the compiler, never NULL. */
const char *pal_platform_get_compiler_name(void);

/* Human readable name of the CPU architecture, never NULL. */
const char *pal_platform_get_arch_name(void);

/* Measures byte order at run time (used to verify PAL_BYTE_ORDER). */
bool pal_platform_probe_little_endian(void);

#ifdef __cplusplus
}
#endif

#endif /* PAL_PLATFORM_H */
