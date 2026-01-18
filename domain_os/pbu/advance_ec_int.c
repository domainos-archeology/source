/*
 * PBU_$ADVANCE_EC_INT - Advance a PBU eventcount (interrupt-level)
 *
 * Validates the eventcount index and owner, then advances the eventcount.
 *
 * Reverse engineered from Domain/OS at address 0x00e88400
 */

#include "pbu/pbu.h"

/*
 * External reference to PBU eventcount array
 * This is the EC2_$PBU_ECS array at 0xE88460
 */
extern pbu_ec_entry_t PBU_$EC_ARRAY[];

/*
 * PBU_$ADVANCE_EC_INT
 *
 * This function advances a PBU eventcount after validating:
 * 1. The eventcount index is in valid range (0x101-0x120)
 * 2. The owner ID matches the expected value
 *
 * If validation fails, status is set to status_$ec2_bad_event_count.
 * If validation succeeds, the eventcount is advanced without
 * triggering a dispatch (suitable for interrupt-level calls).
 */
void PBU_$ADVANCE_EC_INT(
    int16_t *owner_id_ptr,
    uint32_t *ec_index_ptr,
    status_$t *status_ret)
{
    uint32_t ec_index;
    int16_t array_index;
    pbu_ec_entry_t *entry;

    /* Default to bad eventcount status */
    *status_ret = status_$ec2_bad_event_count;

    ec_index = *ec_index_ptr;

    /*
     * Validate index is in PBU range (0x101 to 0x120)
     */
    if (ec_index < PBU_EC_INDEX_MIN) {
        return;
    }
    if (ec_index > PBU_EC_INDEX_MAX) {
        return;
    }

    /*
     * Calculate array index and get entry pointer
     * array_index = (ec_index - 0x101)
     */
    array_index = (int16_t)(ec_index - PBU_EC_INDEX_MIN);
    entry = &PBU_$EC_ARRAY[array_index];

    /*
     * Validate owner ID matches
     */
    if (entry->owner_id != *owner_id_ptr) {
        return;
    }

    /*
     * Validation passed - advance the eventcount
     */
    *status_ret = status_$ok;
    EC_$ADVANCE_WITHOUT_DISPATCH(&entry->ec);
}
