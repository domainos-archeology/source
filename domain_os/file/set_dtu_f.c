/*
 * FILE_$SET_DTU_F - Set Data Time Used (full version)
 *
 * Sets the DTU (access time) attribute.
 *
 * Original address: 0x00E5E2C2
 */

#include "file/file_internal.h"

/*
 * Time value structure for DTM/DTU
 */
typedef struct time_value_t {
    uint32_t high;
    uint16_t low;
} time_value_t;

/*
 * FILE_$SET_DTU_F
 *
 * Sets the Data Time Used (DTU/access time) attribute of a file.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   time_value - Pointer to time value (32-bit high + 16-bit low)
 *   status_ret - Receives operation status
 *
 * If status is status_$ast_incompatible_request, falls back to
 * AST_$SET_ATTRIBUTE with attr_id=10 (rounding up if fractional).
 */
void FILE_$SET_DTU_F(uid_t *file_uid, void *time_value, status_$t *status_ret)
{
    time_value_t local_time;

    /* Copy the time value */
    local_time.high = ((time_value_t *)time_value)->high;
    local_time.low = ((time_value_t *)time_value)->low;

    /* Set attribute 24 (DTU full format) with flags 0xFFFF */
    FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_DTU_FULL, &local_time,
                        0x0000FFFF, status_ret);

    /* If incompatible request, fall back to AST_$SET_ATTRIBUTE */
    if (*status_ret == status_$ast_incompatible_request) {
        /* Round up if there's a fractional portion */
        if (local_time.low != 0) {
            local_time.high += 1;
        }
        /* Use AST compat mode attr_id = 10 */
        AST_$SET_ATTRIBUTE(file_uid, FILE_ATTR_DTU_AST, &local_time.high,
                           status_ret);
    }
}
