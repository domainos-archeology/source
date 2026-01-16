/*
 * FILE_$SET_DTM - Set Data Time Modified (simple version)
 *
 * Wrapper for FILE_$SET_DTM_F that takes a simple 32-bit time value.
 *
 * Original address: 0x00E5E28E
 */

#include "file/file_internal.h"

/*
 * Internal constant used as flags parameter
 * Located at 0x00E5D380 in original code
 */
static const int8_t DTM_FLAGS_EXPLICIT = 0;

/*
 * FILE_$SET_DTM
 *
 * Sets the Data Time Modified (DTM) attribute using an explicit time.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   time_value - Pointer to time value (uint32_t)
 *   status_ret - Receives operation status
 *
 * The time value is extended to include a zero fractional portion.
 */
void FILE_$SET_DTM(uid_t *file_uid, uint32_t *time_value, status_$t *status_ret)
{
    struct {
        uint32_t high;
        uint16_t low;
    } local_time;
    int8_t flags;

    /* Copy the time value and set fractional to 0 */
    local_time.high = *time_value;
    local_time.low = 0;

    /* Use explicit time mode */
    flags = DTM_FLAGS_EXPLICIT;

    FILE_$SET_DTM_F(file_uid, &flags, &local_time, status_ret);
}
