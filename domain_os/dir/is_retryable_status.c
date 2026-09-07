/*
 * DIR_$IS_RETRYABLE_STATUS - Check if status code is retryable
 *
 * Tests whether a given status code represents a transient error
 * that should be retried (e.g., communication failure, address
 * space exhaustion, or stale directory reference).
 *
 * Returns non-zero (negative, 0xFF) if the status is retryable,
 * zero if it is not.
 *
 * Retryable status codes:
 *   0x000E002B - directory not local
 *   0x000E0020 - directory not found in pathname
 *   0x000E0033 - directory object not found
 *   0x000F0003 - bad reply received from remote node
 *   0x000F0004 - communications problem with remote node
 *   subsystem 0x11 (byte 1 == 0x11) - network subsystem errors
 *
 * Parameters:
 *   status - Status code to check
 *
 * Returns:
 *   Negative (0xFF) if retryable, 0 if not
 *
 * Original address: 0x00E4BC26
 * Original size: 80 bytes
 */

#include "dir/dir_internal.h"

/*
 * The five longwords compared at 0x00E4BC2A..0x00E4BC60 are
 * 0xE002B, 0xE0020, 0xE0033, 0xF0003 and 0xF0004; all five constants come
 * from name/name.h and file/file.h.  The guarded copies that used to sit here
 * had 0xF0003 and 0xF0004 the wrong way round (they were inert because
 * dir_internal.h pulls in file/file.h first).
 */

int8_t DIR_$IS_RETRYABLE_STATUS(status_$t status)
{
    int8_t result = 0;

    if (status == status_$naming_directory_not_local) {
        result = (int8_t)0xFF;
    }
    if (status == status_$naming_directory_not_found_in_pathname) {
        result = (int8_t)0xFF;
    }
    if (status == status_$naming_directory_object_not_found) {  /* 0x000E0033 */
        result = (int8_t)0xFF;
    }
    if (status == file_$bad_reply_received_from_remote_node) {   /* 0xF0003 */
        result = (int8_t)0xFF;
    }
    if (status == file_$comms_problem_with_remote_node) {        /* 0xF0004 */
        result = (int8_t)0xFF;
    }

    /* Check if subsystem byte (byte 1, bits 16-23) is 0x11 (network) */
    if (((status >> 16) & 0xFF) == 0x11) {
        result = (int8_t)0xFF;
    }

    return result;
}
