/**
 * @file    pal_error.c
 * @brief   Implementation of the PAL status code helpers.
 */

#include <stddef.h>      /* NULL, size_t */

#include "pal_error.h"   /* public API of this module              */
#include "pal_types.h"   /* PAL_ARRAY_SIZE, PAL_STATIC_ASSERT      */

/* ------------------------------------------------------------------ */
/* Compile time guarantees of the collision free design                */
/* ------------------------------------------------------------------ */

/* The status type must be exactly 32 bit wide on every target. */
PAL_STATIC_ASSERT(sizeof(pal_status_t) == 4u, pal_status_must_be_32_bit);

/* The first and the last defined error must lie inside the reserved block. */
PAL_STATIC_ASSERT(PAL_ERR_UNKNOWN <= PAL_ERR_RANGE_MAX,
                  pal_first_error_inside_reserved_block);

PAL_STATIC_ASSERT(PAL_ERR_RESOURCE_EXHAUSTED >= PAL_ERR_RANGE_MIN,
                  pal_last_error_inside_reserved_block);

/* The whole block must stay far away from errno (<= 4095) and Win32 (<= 65535)
 * magnitudes, i.e. every code is below -65535. */
PAL_STATIC_ASSERT(PAL_ERR_RANGE_MAX < -65535,
                  pal_block_does_not_overlap_errno_or_win32);

/* ------------------------------------------------------------------ */
/* Lookup table (single source of truth for name <-> code)             */
/* ------------------------------------------------------------------ */

/** One row of the status code table. */
typedef struct
{
    pal_status_t code;   /**< Numeric status value.   */
    const char  *name;   /**< Symbolic name of it.    */
} pal_error_entry_t;

/** Name returned for values that are not defined by the PAL. */
static const char k_unrecognized_name[] = "PAL_UNRECOGNIZED_STATUS";

/** All defined status codes. Keep in sync with the enum in pal_error.h. */
static const pal_error_entry_t k_error_table[] =
{
    { PAL_OK,                      "PAL_OK"                      },
    { PAL_ERR_UNKNOWN,             "PAL_ERR_UNKNOWN"             },
    { PAL_ERR_INVALID_ARGUMENT,    "PAL_ERR_INVALID_ARGUMENT"    },
    { PAL_ERR_NULL_POINTER,        "PAL_ERR_NULL_POINTER"        },
    { PAL_ERR_OUT_OF_MEMORY,       "PAL_ERR_OUT_OF_MEMORY"       },
    { PAL_ERR_NOT_SUPPORTED,       "PAL_ERR_NOT_SUPPORTED"       },
    { PAL_ERR_NOT_IMPLEMENTED,     "PAL_ERR_NOT_IMPLEMENTED"     },
    { PAL_ERR_NOT_INITIALIZED,     "PAL_ERR_NOT_INITIALIZED"     },
    { PAL_ERR_ALREADY_INITIALIZED, "PAL_ERR_ALREADY_INITIALIZED" },
    { PAL_ERR_INVALID_STATE,       "PAL_ERR_INVALID_STATE"       },
    { PAL_ERR_OUT_OF_RANGE,        "PAL_ERR_OUT_OF_RANGE"        },
    { PAL_ERR_BUFFER_TOO_SMALL,    "PAL_ERR_BUFFER_TOO_SMALL"    },
    { PAL_ERR_OVERFLOW,            "PAL_ERR_OVERFLOW"            },
    { PAL_ERR_TIMEOUT,             "PAL_ERR_TIMEOUT"             },
    { PAL_ERR_BUSY,                "PAL_ERR_BUSY"                },
    { PAL_ERR_WOULD_BLOCK,         "PAL_ERR_WOULD_BLOCK"         },
    { PAL_ERR_INTERRUPTED,         "PAL_ERR_INTERRUPTED"         },
    { PAL_ERR_NOT_FOUND,           "PAL_ERR_NOT_FOUND"           },
    { PAL_ERR_ALREADY_EXISTS,      "PAL_ERR_ALREADY_EXISTS"      },
    { PAL_ERR_ACCESS_DENIED,       "PAL_ERR_ACCESS_DENIED"       },
    { PAL_ERR_IO,                  "PAL_ERR_IO"                  },
    { PAL_ERR_RESOURCE_EXHAUSTED,  "PAL_ERR_RESOURCE_EXHAUSTED" },
    { PAL_ERR_CONNECTION_REFUSED,  "PAL_ERR_CONNECTION_REFUSED" },
    { PAL_ERR_ADDRESS_IN_USE,      "PAL_ERR_ADDRESS_IN_USE" },
    { PAL_ERR_NOT_CONNECTED,       "PAL_ERR_NOT_CONNECTED" },
    { PAL_ERR_NETWORK_UNREACHABLE, "PAL_ERR_NETWORK_UNREACHABLE" }
};

/**
 * @brief      Finds the table row of a status code.
 *
 * @details    Internal helper shared by the public functions so the lookup
 *             logic exists exactly once. A linear search is used on purpose:
 *             the table is tiny, needs no sorting and no dynamic memory,
 *             which keeps it usable on bare metal targets.
 *
 * @param[in]  status  Status value to look for.
 *
 * @return     Pointer to the matching table row, or NULL if the value is not
 *             defined by the PAL.
 * @retval     NULL    The value is not a defined PAL status.
 *
 * @note       The returned pointer refers to constant static storage and
 *             stays valid for the whole program lifetime.
 */
static const pal_error_entry_t *pal_error_find(pal_status_t status)
{
    size_t index;

    /* Walk every row until the numeric code matches. */
    for (index = 0u; index < PAL_ARRAY_SIZE(k_error_table); index++)
    {
        if (k_error_table[index].code == status)
        {
            return &k_error_table[index];
        }
    }

    /* Nothing matched: the value is foreign to the PAL. */
    return NULL;
}

/**
 * @brief      Converts a status code to its symbolic name.
 *
 * @details    Useful for logging and debugging, e.g. printing
 *             "PAL_ERR_TIMEOUT" instead of -5259533. The function is pure,
 *             re-entrant and thread safe because it only reads constant data.
 *
 * @param[in]  status  Status value to convert. Any int32 value is accepted,
 *                     including values that do not belong to the PAL.
 *
 * @return     Pointer to a NUL terminated, statically allocated string.
 * @retval     "PAL_UNRECOGNIZED_STATUS"  The value is not defined by the PAL
 *                                        (for example a raw errno value).
 *
 * @note       The function never returns NULL and the caller must not free or
 *             modify the returned string.
 */
const char *pal_error_to_string(pal_status_t status)
{
    const pal_error_entry_t *entry = pal_error_find(status);

    /* Defined code: return its name, otherwise the generic placeholder. */
    return (entry != NULL) ? entry->name : k_unrecognized_name;
}

/**
 * @brief      Tells whether a value is a status defined by the PAL.
 *
 * @details    Lets callers detect when a foreign error number (errno, Win32,
 *             HRESULT, ...) was accidentally passed on as a PAL status
 *             instead of being translated by a backend first.
 *
 * @param[in]  status  Status value to check.
 *
 * @return     true if the value is PAL_OK or one of the PAL_ERR_* codes.
 * @retval     true    The value is defined by the PAL.
 * @retval     false   The value is unknown to the PAL.
 *
 * @note       Thread safe, no side effects.
 */
bool pal_error_is_defined(pal_status_t status)
{
    return (pal_error_find(status) != NULL);
}
