/*
 * smd/dm_cond_event_wait.c - SMD_$DM_COND_EVENT_WAIT implementation
 *
 * Conditional wait for display manager events. Checks multiple event
 * sources including display units, power off, and request queues.
 *
 * Original address: 0x00E6EFF0
 */

#include "smd/smd_internal.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "mmu/mmu.h"

/*
 * SMD_$DM_COND_EVENT_WAIT - Conditional event wait for display manager
 *
 * Checks multiple event sources and returns when any event is available:
 * - Display unit events (cursor updates, etc.)
 * - Power off events
 * - Request queue messages
 * - Display buffer completion
 *
 * Parameters:
 *   event_type  - Output: receives event type code
 *   param2      - Output: receives display unit number or request type
 *   param3      - Output: receives parameter count or additional data
 *   status_ret  - Output: receives status code
 *
 * Event types returned:
 *   1 = Request queue message available
 *   2 = Display buffer event
 *   3 = Display unit event (cursor flag set)
 *   4 = Display unit event (tracking flag set)
 *   6 = Power off event
 *   9 = No event / timeout
 */
void SMD_$DM_COND_EVENT_WAIT(uint16_t *event_type, int16_t *param2,
                             int16_t *param3, status_$t *status_ret)
{
    uint16_t asid;
    int16_t counter;
    uint16_t unit_num;
    smd_display_unit_t *unit_ptr;
    smd_display_hw_t *hw;
    uint16_t hw_flags;
    int8_t power_status;
    int entry_idx;
    smd_request_entry_t *req_entry;
    int16_t param_idx;

    /* Initialize status */
    *status_ret = status_$ok;

    /* Get current process ASID */
    asid = PROC1_$AS_ID;

    /* Scan through display units looking for events */
    counter = 0;
    unit_num = 1;
    unit_ptr = &SMD_DISPLAY_UNITS[1];  /* Units are 1-based */

    while (1) {
        /* Check if this unit is associated with our ASID */
        if (unit_ptr->hw != NULL && asid == unit_ptr->asid) {
            hw = unit_ptr->hw;
            hw_flags = hw->field_4c;

            /* Check for tracking flag (bit 14) */
            if (hw_flags & 0x4000) {
                *event_type = 4;
                hw->field_4c &= ~0x4000;  /* Clear flag */
                *param2 = unit_num;
                return;
            }

            /* Check for cursor flag (bit 15) */
            if ((int16_t)hw_flags < 0) {  /* Test sign bit */
                *event_type = 3;
                hw->field_4c &= ~0x8000;  /* Clear flag */
                *param2 = unit_num;
                return;
            }
        }

        /* Move to next unit */
        unit_num++;
        unit_ptr++;
        counter--;

        /* Check if we've scanned all units */
        if (counter == -1) {
            break;
        }
    }

    /* Check for power off event */
    power_status = MMU_$POWER_OFF();
    if (power_status < 0 && SMD_GLOBALS.power_off_reported >= 0) {
        /* Power off detected and not yet reported */
        *event_type = SMD_EVTYPE_POWER_OFF;
        SMD_GLOBALS.power_off_reported = power_status;
        return;
    }
    SMD_GLOBALS.power_off_reported = power_status;

    /* Check request queue for pending requests */
    if (SMD_GLOBALS.request_queue_head != SMD_GLOBALS.request_queue_tail) {
        *event_type = 1;  /* Request queue message */

        /* Acquire lock for queue access */
        ML_$LOCK(SMD_REQUEST_LOCK);

        if (SMD_GLOBALS.request_queue_head != SMD_GLOBALS.request_queue_tail) {
            /* Get current entry */
            entry_idx = SMD_GLOBALS.request_queue_tail;
            req_entry = &SMD_GLOBALS.request_queue[entry_idx];

            /* Copy request type and param count */
            *param2 = req_entry->request_type;
            *param3 = req_entry->param_count;

            /* Copy parameters if any */
            param_idx = *param3 - 1;
            if (param_idx >= 0) {
                int16_t *dest = param2 + 2;  /* Params start after type/count */
                int copy_idx = 0;
                do {
                    dest[copy_idx] = req_entry->params[copy_idx];
                    copy_idx++;
                    param_idx--;
                } while (param_idx >= 0);
            }

            /* Advance tail pointer (circular) */
            if (SMD_GLOBALS.request_queue_tail >= SMD_REQUEST_QUEUE_MAX) {
                SMD_GLOBALS.request_queue_tail = 1;
            } else {
                SMD_GLOBALS.request_queue_tail++;
            }

            ML_$UNLOCK(SMD_REQUEST_LOCK);

            /* Signal that we consumed a request */
            EC_$ADVANCE(&SMD_EC_1);
            return;
        }

        ML_$UNLOCK(SMD_REQUEST_LOCK);
    }

    /* Check if current process has an associated display unit */
    if (SMD_GLOBALS.asid_to_unit[asid] == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* Check display buffer event count */
    {
        uint16_t unit_idx = SMD_GLOBALS.asid_to_unit[asid];
        smd_display_unit_t *disp_unit = &SMD_DISPLAY_UNITS[unit_idx];
        smd_display_hw_t *disp_hw = disp_unit->hw;

        if (asid == disp_unit->asid &&
            disp_hw->lock_ec.count + 1 <= disp_hw->op_ec.count) {
            /* Buffer operation completed */
            *event_type = 2;
            disp_hw->field_1c = disp_hw->op_ec.count;
            disp_unit->asid = 0;  /* Clear association */
        } else {
            /* No event available */
            *event_type = SMD_EVTYPE_SIGNAL;
        }
    }
}
