/*
 * ACL_$SET_LOCAL_LOCKSMITH - Set local locksmith mode
 *
 * Sets the local locksmith state. Only processes with the locksmith UID
 * (in their login, group, or user SID) can call this function.
 *
 * Parameters:
 *   locksmith_value - Pointer to new locksmith value
 *   status_ret      - Output status code
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$no_right_to_perform_operation - Caller doesn't have locksmith UID
 *
 * Original address: 0x00E49196
 */

#include "acl/acl_internal.h"

void ACL_$SET_LOCAL_LOCKSMITH(int16_t *locksmith_value, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    acl_sid_block_t *curr_sids = &ACL_$CURRENT_SIDS[pid];
    int16_t asid_index = pid - 1;
    int16_t byte_index;
    uint8_t bit_mask;

    *status_ret = status_$no_right_to_perform_operation;

    /* Check if caller has locksmith UID in login SID */
    if (curr_sids->login_sid.high == RGYC_$G_LOCKSMITH_UID.high &&
        curr_sids->login_sid.low == RGYC_$G_LOCKSMITH_UID.low) {
        goto granted;
    }

    /* Check if caller has locksmith UID in group SID */
    if (curr_sids->group_sid.high == RGYC_$G_LOCKSMITH_UID.high &&
        curr_sids->group_sid.low == RGYC_$G_LOCKSMITH_UID.low) {
        goto granted;
    }

    /* Check if caller has locksmith UID in user SID */
    if (curr_sids->user_sid.high == RGYC_$G_LOCKSMITH_UID.high &&
        curr_sids->user_sid.low == RGYC_$G_LOCKSMITH_UID.low) {
        goto granted;
    }

    /* No locksmith privilege */
    return;

granted:
    *status_ret = status_$ok;

    /* Set the suser bit for this ASID */
    byte_index = asid_index >> 3;
    bit_mask = 0x80 >> (asid_index & 7);
    ACL_$ASID_SUSER_BITMAP[byte_index] |= bit_mask;

    /* Set the local locksmith value */
    ACL_$LOCAL_LOCKSMITH = *locksmith_value;
}
