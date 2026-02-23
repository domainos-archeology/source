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
 *   0x000E002B - naming_directory_not_local
 *   0x000E0020 - naming_directory_not_found_in_pathname
 *   0x000E0033 - naming_acl_not_found (stale object)
 *   0x000F0003 - file_$comms_problem_with_remote_node
 *   0x000F0004 - file_$bad_reply_received_from_remote_node
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

/* Status codes for retryable conditions */
#ifndef status_$naming_directory_not_local
#define status_$naming_directory_not_local       0x000E002B
#endif
#ifndef status_$naming_directory_not_found_in_pathname
#define status_$naming_directory_not_found_in_pathname 0x000E0020
#endif
#ifndef file_$comms_problem_with_remote_node
#define file_$comms_problem_with_remote_node     0x000F0003
#endif
#ifndef file_$bad_reply_received_from_remote_node
#define file_$bad_reply_received_from_remote_node 0x000F0004
#endif

int8_t DIR_$IS_RETRYABLE_STATUS(status_$t status)
{
    int8_t result = 0;

    if (status == status_$naming_directory_not_local) {
        result = (int8_t)0xFF;
    }
    if (status == status_$naming_directory_not_found_in_pathname) {
        result = (int8_t)0xFF;
    }
    if (status == status_$naming_acl_not_found) {  /* 0x000E0033 */
        result = (int8_t)0xFF;
    }
    if (status == file_$comms_problem_with_remote_node) {
        result = (int8_t)0xFF;
    }
    if (status == file_$bad_reply_received_from_remote_node) {
        result = (int8_t)0xFF;
    }

    /* Check if subsystem byte (byte 1, bits 16-23) is 0x11 (network) */
    if (((status >> 16) & 0xFF) == 0x11) {
        result = (int8_t)0xFF;
    }

    return result;
}
