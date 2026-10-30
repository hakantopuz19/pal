/**
 * @file    pal_error.h
 * @brief   Common status / error codes of the PAL.
 *
 * @details Self contained header (standard includes only).
 *
 *          COLLISION FREE DESIGN
 *          Existing systems already use many numeric error spaces:
 *            - C / POSIX errno          : small positive numbers (and -errno
 *                                         in kernels, magnitude <= 4095)
 *            - Win32 GetLastError()     : positive, 0 .. 65535
 *            - HRESULT / NTSTATUS       : 0x8xxxxxxx, 0xCxxxxxxx (negative
 *                                         when viewed as int32_t)
 *            - many APIs                : plain -1 as "failure"
 *          PAL error codes therefore live in a dedicated, reserved block:
 *
 *              -0x00504101 ... -0x005041FF      ("PA" = 0x50 0x41 in the
 *                                               middle bytes)
 *
 *          - magnitude is above 65535  -> cannot equal any errno / Win32 code
 *          - value is far from -1      -> cannot equal "-1" or -errno
 *          - upper byte is 0xFF        -> not a defined HRESULT / NTSTATUS shape
 *          - PAL_OK is 0               -> the universal "success" value that
 *                                         errno, ERROR_SUCCESS, S_OK and
 *                                         STATUS_SUCCESS all share, so it
 *                                         conflicts with none of them
 *
 *          Native error codes must NEVER be returned raw by the PAL; backends
 *          translate them into one of the codes below.
 */

#ifndef PAL_ERROR_H
#define PAL_ERROR_H

#include <stdint.h>   /* int32_t */
#include <stdbool.h>  /* bool    */

#ifdef __cplusplus
extern "C" {
#endif

/** Status type returned by PAL functions: PAL_OK or a PAL_ERR_* value. */
typedef int32_t pal_status_t;

/**
 * @brief Status code constants.
 *
 * @details Success is PAL_OK (0). Every failure is a negative value inside the
 *          reserved block described in the file documentation. Never reorder
 *          or renumber existing codes (they are part of the public contract);
 *          append new ones at the end only.
 */
enum
{
    PAL_OK                   = 0,                      /**< Success.                              */

    PAL_ERR_BASE             = -0x00504100,            /**< Reserved anchor, NOT a valid code.    */

    PAL_ERR_UNKNOWN          = PAL_ERR_BASE - 1,       /**< Unspecified failure.                  */
    PAL_ERR_INVALID_ARGUMENT = PAL_ERR_BASE - 2,       /**< An argument has an invalid value.     */
    PAL_ERR_NULL_POINTER     = PAL_ERR_BASE - 3,       /**< A required pointer was NULL.          */
    PAL_ERR_OUT_OF_MEMORY    = PAL_ERR_BASE - 4,       /**< Memory allocation failed.             */
    PAL_ERR_NOT_SUPPORTED    = PAL_ERR_BASE - 5,       /**< Not supported on this platform.       */
    PAL_ERR_NOT_IMPLEMENTED  = PAL_ERR_BASE - 6,       /**< Supported in theory, not written yet. */
    PAL_ERR_NOT_INITIALIZED  = PAL_ERR_BASE - 7,       /**< Object / module used before init.     */
    PAL_ERR_ALREADY_INITIALIZED = PAL_ERR_BASE - 8,    /**< Object / module initialized twice.    */
    PAL_ERR_INVALID_STATE    = PAL_ERR_BASE - 9,       /**< Operation illegal in current state.   */
    PAL_ERR_OUT_OF_RANGE     = PAL_ERR_BASE - 10,      /**< Value outside the permitted range.    */
    PAL_ERR_BUFFER_TOO_SMALL = PAL_ERR_BASE - 11,      /**< Caller supplied buffer is too small.  */
    PAL_ERR_OVERFLOW         = PAL_ERR_BASE - 12,      /**< Arithmetic or capacity overflow.      */
    PAL_ERR_TIMEOUT          = PAL_ERR_BASE - 13,      /**< Operation timed out.                  */
    PAL_ERR_BUSY             = PAL_ERR_BASE - 14,      /**< Resource is busy, try again later.    */
    PAL_ERR_WOULD_BLOCK      = PAL_ERR_BASE - 15,      /**< Non blocking call would block.        */
    PAL_ERR_INTERRUPTED      = PAL_ERR_BASE - 16,      /**< Operation was interrupted.            */
    PAL_ERR_NOT_FOUND        = PAL_ERR_BASE - 17,      /**< Requested object does not exist.      */
    PAL_ERR_ALREADY_EXISTS   = PAL_ERR_BASE - 18,      /**< Object already exists.                */
    PAL_ERR_ACCESS_DENIED    = PAL_ERR_BASE - 19,      /**< Permission denied.                    */
    PAL_ERR_IO               = PAL_ERR_BASE - 20,      /**< Low level input / output failure.     */
    PAL_ERR_RESOURCE_EXHAUSTED = PAL_ERR_BASE - 21,    /**< A system resource limit was reached.  */
    PAL_ERR_CONNECTION_REFUSED = PAL_ERR_BASE - 22,    /**< A remote endpoint refused a connection. */
    PAL_ERR_ADDRESS_IN_USE     = PAL_ERR_BASE - 23,    /**< The requested address is already in use. */
    PAL_ERR_NOT_CONNECTED      = PAL_ERR_BASE - 24,    /**< The socket is not connected.          */
    PAL_ERR_NETWORK_UNREACHABLE = PAL_ERR_BASE - 25   /**< No route to the requested network.   */
};

/** Closest-to-zero value of the reserved PAL error block. */
#define PAL_ERR_RANGE_MAX ((pal_status_t)(PAL_ERR_BASE - 1))

/** Farthest-from-zero value of the reserved PAL error block. */
#define PAL_ERR_RANGE_MIN ((pal_status_t)(PAL_ERR_BASE - 255))

/** True when the status denotes success (PAL_OK). */
#define PAL_SUCCEEDED(status) ((status) >= 0)

/** True when the status denotes a failure (any PAL_ERR_*). */
#define PAL_FAILED(status) ((status) < 0)

/* Returns the symbolic name of a status code, e.g. "PAL_ERR_TIMEOUT".
 * Never returns NULL. Full documentation: pal_library/src/common/pal_error.c */
const char *pal_error_to_string(pal_status_t status);

/* Returns true when the value is PAL_OK or a defined PAL_ERR_* code.
 * Full documentation: pal_library/src/common/pal_error.c                  */
bool pal_error_is_defined(pal_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* PAL_ERROR_H */
