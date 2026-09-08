/*
 * FILE_$SET_DTM - Set Data Time Modified (simple version)
 *
 * Wrapper for FILE_$SET_DTM_F that takes a simple 32-bit time value.
 *
 * Original address: 0x00E5E28E
 */

#include "file/file_internal.h"

/*
 * 0x00E5D380: the flags byte FILE_$SET_DTM_F is handed by reference
 * (`pea (-0xf30,PC)` at 0x00E5E2AE).  Zero, so FILE_$SET_DTM_F takes its
 * "explicit time" arm ("tst.b (A1) / bpl" at 0x00E5E216).  The image holds
 * ONE such cell, shared with FILE_$SET_ATTRIBUTE and FILE_$PRIV_LOCK's
 * CHECK_RIGHTS helper; it is defined in file/file_data.c as
 * file_$zero_bytes.
 */

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

    /*
     * 0x00E5E29A-0x00E5E2A2: A6-0x8 takes the caller's longword and A6-0x4 is
     * cleared, so the six-byte time record is {*time_value, 0}.
     */
    local_time.high = *time_value;
    local_time.low = 0;

    /*
     * 0x00E5E2A6-0x00E5E2B6: the flags argument is NOT a local - it is the
     * address of the shared in-code zero byte at 0x00E5D380.
     */
    FILE_$SET_DTM_F(file_uid, (int8_t *)&file_$zero_bytes[0], &local_time,
                    status_ret);
}
