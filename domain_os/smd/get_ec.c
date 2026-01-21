/*
 * smd/get_ec.c - SMD_$GET_EC implementation
 *
 * Get event count for various SMD events.
 *
 * Original address: 0x00E6FD90
 *
 * Assembly analysis:
 * The function returns different event counts based on a key value:
 *   0 = DTTE (Display Transfer Table Event)
 *   1 = Display operation event count (from hardware info + 0x10)
 *   2 = SMD_EC_2 (0x00E2E408)
 *   3 = OS_$SHUTDOWN_EC (0x00E1DC00)
 *   default = invalid event count key error
 *
 * The function then calls EC2_$REGISTER_EC1 to convert the EC1 to EC2.
 */

#include "smd/smd_internal.h"
#include "ec2/ec2.h"
#include "os/os.h"

/*
 * Event count key values
 */
#define SMD_EC_KEY_DTTE         0   /* Display Transfer Table Event */
#define SMD_EC_KEY_DISP_OP      1   /* Display operation complete */
#define SMD_EC_KEY_SMD_EC2      2   /* SMD secondary event count */
#define SMD_EC_KEY_SHUTDOWN     3   /* OS shutdown event count */

/* Status code for invalid event count key */
#define status_$display_invalid_event_count_key     0x00130026

/*
 * SMD_$GET_EC - Get event count
 *
 * Returns an EC2 (user-mode event count) for the specified SMD event.
 *
 * Parameters:
 *   key        - Pointer to event count key (0-3)
 *   ec2_ret    - Output: receives the EC2 handle
 *   status_ret - Output: status return
 *
 * Key values:
 *   0 = Display Transfer Table Event (DTTE)
 *   1 = Display operation complete (for current process's display)
 *   2 = SMD secondary event count
 *   3 = OS shutdown event count
 */
void SMD_$GET_EC(uint16_t *key, void **ec2_ret, status_$t *status_ret)
{
    uint16_t unit;
    int32_t unit_offset;
    ec_$eventcount_t *ec1;
    smd_display_hw_t *hw;

    /* Get current process's display unit */
    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    *status_ret = status_$ok;

    /* Calculate unit offset and get hardware info */
    unit_offset = (int32_t)unit * SMD_DISPLAY_UNIT_SIZE;
    hw = (smd_display_hw_t *)((uint8_t *)SMD_UNIT_AUX_BASE + unit_offset);

    switch (*key) {
    case SMD_EC_KEY_DTTE:
        /* Display Transfer Table Event */
        ec1 = &DTTE;
        break;

    case SMD_EC_KEY_DISP_OP:
        /* Display operation complete - from hardware info at offset +0x10 */
        ec1 = &hw->op_ec;
        break;

    case SMD_EC_KEY_SMD_EC2:
        /* SMD secondary event count */
        ec1 = &SMD_EC_2;
        break;

    case SMD_EC_KEY_SHUTDOWN:
        /* OS shutdown event count */
        ec1 = &OS_$SHUTDOWN_EC;
        break;

    default:
        /* Invalid event count key */
        *status_ret = status_$display_invalid_event_count_key;
        return;
    }

    /* Register EC1 as EC2 for user-mode access */
    *ec2_ret = EC2_$REGISTER_EC1(ec1, status_ret);
}
