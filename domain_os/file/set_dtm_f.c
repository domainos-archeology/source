/*
 * FILE_$SET_DTM_F - Set Data Time Modified (full version)
 *
 * Sets the DTM attribute with full control over the value.
 * If flags indicate use of current time, the current clock is used.
 *
 * Original address: 0x00E5E1EE
 */

#include "file/file_internal.h"

/*
 * Time value structure for DTM/DTU
 *
 * The time value is a 48-bit value stored as:
 *   - high: 32-bit seconds/high portion
 *   - low: 16-bit fractional/low portion
 */
typedef struct time_value_t {
    uint32_t high;
    uint16_t low;
} time_value_t;

/*
 * FILE_$SET_DTM_F
 *
 * Sets the Data Time Modified (DTM) attribute of a file.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   flags      - Pointer to flags byte (negative = use current time)
 *   time_value - Pointer to time value
 *   status_ret - Receives operation status
 *
 * Behavior:
 *   - If *flags < 0: Uses current clock time, attr_id = 26 (0x1A)
 *   - Otherwise: Uses provided time value, attr_id = 23 (0x17)
 *
 * If status is status_$ast_incompatible_request, falls back to
 * AST_$SET_ATTRIBUTE with attr_id=9 (rounding up if fractional).
 */
void FILE_$SET_DTM_F(uid_t *file_uid, int8_t *flags, void *time_value,
                     status_$t *status_ret)
{
    time_value_t local_time;
    int16_t attr_id;
    uint16_t option_flags;

    /* Copy the time value */
    local_time.high = ((time_value_t *)time_value)->high;
    local_time.low = ((time_value_t *)time_value)->low;

    if (*flags < 0) {
        /* Use current time */
        option_flags = 10;  /* 0x0A */
        local_time.high = TIME_$CURRENT_CLOCKH;
        local_time.low = 0;
        attr_id = FILE_ATTR_DTM_CURRENT;  /* 26 = 0x1A */
    } else {
        /* Use provided time */
        option_flags = 8;  /* 0x08 */
        attr_id = FILE_ATTR_DTM_OLD;  /* 23 = 0x17 */
    }

    /* Set the attribute
     * Flags = (option_flags << 16) | 0xFFFF
     */
    FILE_$SET_ATTRIBUTE(file_uid, attr_id, &local_time,
                        ((uint32_t)option_flags << 16) | 0xFFFF, status_ret);

    /* If incompatible request, fall back to AST_$SET_ATTRIBUTE */
    if (*status_ret == status_$ast_incompatible_request) {
        /* Round up if there's a fractional portion */
        if (local_time.low != 0) {
            local_time.high += 1;
        }
        /* Use AST compat mode attr_id = 9 */
        AST_$SET_ATTRIBUTE(file_uid, FILE_ATTR_DTM_AST, &local_time.high,
                           status_ret);
    }
}
