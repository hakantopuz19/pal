/**
 * @file    pal_platform.c
 * @brief   Runtime queries that mirror the compile time platform detection.
 *
 * @details Exposing the detection results as functions makes them testable:
 *          a test can compare what the preprocessor decided with what the
 *          running program actually observes.
 */

#include "pal_platform.h"   /* public API and detection macros */

/**
 * @brief      Returns the name of the target operating system.
 *
 * @details    The name is derived from the compile time macro PAL_OS_ID, so it
 *             reflects the platform the library was BUILT for.
 *
 * @return     Pointer to a NUL terminated, statically allocated string.
 * @retval     "Windows"     Built for Microsoft Windows.
 * @retval     "Linux"       Built for Linux.
 * @retval     "macOS"       Built for Apple macOS.
 * @retval     "RTOS"        Built with PAL_TARGET_RTOS.
 * @retval     "Bare metal"  Built with PAL_TARGET_BAREMETAL.
 *
 * @note       Never returns NULL. Thread safe.
 */
const char *pal_platform_get_os_name(void)
{
#if PAL_OS_WINDOWS
    return "Windows";
#elif PAL_OS_LINUX
    return "Linux";
#elif PAL_OS_MACOS
    return "macOS";
#elif PAL_OS_RTOS
    return "RTOS";
#else
    return "Bare metal";
#endif
}

/**
 * @brief      Returns the name of the compiler used to build the library.
 *
 * @return     Pointer to a NUL terminated, statically allocated string.
 * @retval     "MSVC"     Microsoft Visual C.
 * @retval     "GCC"      GNU GCC.
 * @retval     "Clang"    LLVM Clang.
 * @retval     "Unknown"  Compiler is not recognized.
 *
 * @note       Never returns NULL. Thread safe.
 */
const char *pal_platform_get_compiler_name(void)
{
#if PAL_COMPILER_MSVC
    return "MSVC";
#elif PAL_COMPILER_GCC
    return "GCC";
#elif PAL_COMPILER_CLANG
    return "Clang";
#else
    return "Unknown";
#endif
}

/**
 * @brief      Returns the name of the target CPU architecture.
 *
 * @return     Pointer to a NUL terminated, statically allocated string.
 * @retval     "x86"      32 bit x86.
 * @retval     "x64"      64 bit x86.
 * @retval     "ARM"      32 bit ARM.
 * @retval     "ARM64"    64 bit ARM.
 * @retval     "RISC-V"   RISC-V.
 * @retval     "Unknown"  Architecture is not recognized.
 *
 * @note       Never returns NULL. Thread safe.
 */
const char *pal_platform_get_arch_name(void)
{
#if PAL_ARCH_X86
    return "x86";
#elif PAL_ARCH_X64
    return "x64";
#elif PAL_ARCH_ARM
    return "ARM";
#elif PAL_ARCH_ARM64
    return "ARM64";
#elif PAL_ARCH_RISCV
    return "RISC-V";
#else
    return "Unknown";
#endif
}

/**
 * @brief      Measures the byte order of the running machine.
 *
 * @details    Stores the 16 bit value 1 and inspects its first byte through a
 *             character pointer (always allowed by the C aliasing rules).
 *             The result must always agree with the compile time macro
 *             PAL_IS_LITTLE_ENDIAN; a mismatch means PAL_FORCE_* was set
 *             wrongly in the build system.
 *
 * @return     true if the target stores the least significant byte first.
 * @retval     true    Little endian.
 * @retval     false   Big endian.
 *
 * @note       Thread safe, no side effects.
 */
bool pal_platform_probe_little_endian(void)
{
    /* Value whose two bytes differ, so their order is observable. */
    const uint16_t probe = 0x0001u;

    /* Look at the lowest addressed byte of the value. */
    const unsigned char *first_byte = (const unsigned char *)&probe;

    /* Little endian machines keep the 0x01 in the first byte. */
    return (*first_byte == 0x01u);
}
