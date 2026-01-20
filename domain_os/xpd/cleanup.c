/*
 * XPD Cleanup and Event Posting Functions
 *
 * These functions handle process cleanup during exit and event
 * posting from target to debugger.
 *
 * Original addresses:
 *   XPD_$CLEANUP:      0x00e75046
 *   XPD_$POST_EVENT:   0x00e75090
 */

#include "xpd/xpd.h"
#include "proc1/proc1.h"
#include "ec/ec.h"

/* XPD data base */
#define XPD_DATA_BASE           0xEA5034

/* Target state offsets */
#define TARGET_STATE_BASE       0xEA5044     /* First target state entry */
#define TARGET_STATE_SIZE       0x14         /* Size per target entry */

/* Target state bit fields */
#define TARGET_FLAG_ENABLED     0x80         /* Bit 7: debugging enabled */
#define TARGET_FLAG_PROCESSED   0x40         /* Bit 6: event was processed */
#define TARGET_DEBUGGER_MASK    0x0E         /* Bits 1-3: debugger index << 1 */
#define EVENT_CODE_MASK         0x1E0        /* Bits 5-8: event code */
#define RESPONSE_MASK           0x30         /* Bits 4-5: debugger response */

/* Offsets within target entry */
#define TARGET_EC_OFFSET        0x00         /* Eventcount */
#define TARGET_STATUS_OFFSET    0x0C         /* Event status */
#define TARGET_STATE_OFFSET     0x10         /* State flags */

/* Special status values for event posting */
#define XPD_POST_CLEANUP_MSG1   0xE7508A     /* Cleanup message 1 */
#define XPD_POST_CLEANUP_MSG2   0xE7508C     /* Cleanup message 2 */

/*
 * XPD_$CLEANUP - Clean up debug state when process exits
 *
 * Called during process termination to:
 * 1. Post a cleanup event to any waiting debugger
 * 2. Unregister as a debugger if registered
 * 3. Clear debug flags for this process's target entry
 */
void XPD_$CLEANUP(void)
{
    int32_t target_offset;
    uint16_t *target_state;
    xpd_$response_t response;
    status_$t cleanup_status;
    status_$t unreg_status;

    /* Post cleanup event to debugger (if being debugged) */
    XPD_$POST_EVENT((xpd_$event_type_t *)XPD_POST_CLEANUP_MSG1,
                    (status_$t *)XPD_POST_CLEANUP_MSG2,
                    &response);

    /* Unregister ourselves as a debugger (releases all our targets) */
    XPD_$UNREGISTER_DEBUGGER(PROC1_$AS_ID, &unreg_status);

    /* Clear debug flags in our own target entry */
    target_offset = PROC1_$AS_ID * TARGET_STATE_SIZE;
    target_state = (uint16_t *)(TARGET_STATE_BASE + target_offset);

    /*
     * Clear bits:
     *   Bit 7: enabled
     *   Bits 5-8: event code
     *   Bits 9-10: additional flags
     * Preserve:
     *   Bits 0-4: debugger index and other state
     *   Bits 12-15: other flags
     *
     * Mask 0x701F = 0111 0000 0001 1111
     * This clears bits 5-11 (event code and related flags)
     */
    *target_state &= 0x701F;
}

/*
 * XPD_$POST_EVENT - Post an event from target to debugger
 *
 * Called by a target process to send an event to its debugger.
 * The target suspends until the debugger responds.
 *
 * Parameters:
 *   event_type   - Type of event being posted
 *   status_val   - Status value associated with the event
 *   response_ret - Debugger's response (output)
 */
void XPD_$POST_EVENT(xpd_$event_type_t *event_type, status_$t *status_val,
                     xpd_$response_t *response_ret)
{
    int32_t target_offset;
    uint8_t *target_state_byte;
    uint16_t *target_state_word;
    ec_$eventcount_t *target_ec;
    ec_$eventcount_t *debugger_ec;
    int16_t debugger_idx;
    uint8_t event_code;
    ec_$eventcount_t *ecs[3];
    int32_t wait_val;

    /* Calculate our target state location */
    target_offset = PROC1_$AS_ID * TARGET_STATE_SIZE;
    target_state_byte = (uint8_t *)(TARGET_STATE_BASE + target_offset);
    target_state_word = (uint16_t *)(TARGET_STATE_BASE + target_offset);
    target_ec = (ec_$eventcount_t *)(XPD_DATA_BASE + target_offset);

    /* Check if we have a debugger and are in debug mode */
    debugger_idx = (target_state_byte[0] & TARGET_DEBUGGER_MASK) >> 1;

    if (debugger_idx == 0 || (int16_t)*target_state_word >= 0) {
        /* No debugger or not enabled - return error response */
        *response_ret = 2;  /* Error response code */
        return;
    }

    /* Clear target EC value (reset to 0) */
    *(int32_t *)target_ec = 0;

    /* Get event code from event_type parameter (byte at offset 1) */
    event_code = ((uint8_t *)event_type)[1];

    /* Clear current event code and set new one */
    *target_state_word &= ~EVENT_CODE_MASK;
    *target_state_word |= ((uint16_t)event_code << 5);

    /* Store the event status */
    *(status_$t *)(TARGET_STATE_BASE + target_offset - 0x10 + TARGET_STATUS_OFFSET) = *status_val;

    /* Clear the "processed" flag so debugger can see it */
    target_state_byte[0] &= ~TARGET_FLAG_PROCESSED;

    /* Advance debugger's EC to notify it */
    debugger_ec = (ec_$eventcount_t *)(XPD_DATA_BASE + (debugger_idx << 4) + 0x478);
    EC_$ADVANCE(debugger_ec);

    /* Wait on our own EC for debugger response */
    ecs[0] = target_ec;
    ecs[1] = NULL;
    ecs[2] = NULL;
    wait_val = 1;

    EC_$WAIT(ecs, &wait_val);

    /* Return the debugger's response (bits 4-5 of state byte) */
    *response_ret = (target_state_byte[0] & RESPONSE_MASK) >> 4;
}
