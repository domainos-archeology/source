/*
 * acl_$is_process_type_2 - Check if process is type 2 (user process)
 *
 * Returns non-zero if the specified process is type 2.
 * Type 2 appears to be a regular user process as opposed to
 * system/kernel processes.
 *
 * Parameters:
 *   pid - Process ID to check
 *
 * Returns:
 *   Non-zero (-1) if process type != 2, 0 if type == 2
 *
 * Original address: 0x00E46498
 */

#include "acl/acl_internal.h"

int8_t acl_$is_process_type_2(int16_t pid)
{
    return (PROC1_$TYPE[pid] != 2) ? -1 : 0;
}
