/*
 * FILE_$SET_DTU - Set Data Time Used (simple version)
 *
 * Wrapper for FILE_$SET_DTU_F that takes a simple 32-bit time value.
 *
 * Original address: 0x00E5E332
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_DTU
 *
 * Sets the Data Time Used (DTU/access time) attribute.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   time_value - Pointer to time value (uint32_t)
 *   status_ret - Receives operation status
 *
 * The time value is extended to include a zero fractional portion.
 */
void FILE_$SET_DTU(uid_t *file_uid, uint32_t *time_value, status_$t *status_ret)
{
    struct {
        uint32_t high;
        uint16_t low;
    } local_time;

    /* Copy the time value and set fractional to 0 */
    local_time.high = *time_value;
    local_time.low = 0;

    FILE_$SET_DTU_F(file_uid, &local_time, status_ret);
}
