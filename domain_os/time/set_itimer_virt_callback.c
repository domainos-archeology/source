/*
 * TIME_$SET_ITIMER_VIRT_CALLBACK - Callback for virtual itimer
 *
 * Called when the virtual interval timer expires.
 * Sends SIGVTALRM to the process if the timer has an interval set.
 *
 * Parameters:
 *   arg - Pointer to callback argument containing AS info
 *
 * Original address: 0x00e58a98
 */

#include "time/time_internal.h"

void TIME_$SET_ITIMER_VIRT_CALLBACK(void *arg)
{
    uint32_t **arg_ptr = (uint32_t **)arg;
    uint32_t *inner = *arg_ptr;
    uint16_t as_id = (uint16_t)inner[0];
    int16_t as_offset;
    uint8_t *itimer_entry;
    uint32_t interval_high;
    uint16_t interval_low;
    status_$t status;

    /* Get the itimer entry for this AS */
    as_offset = as_id * ITIMER_DB_ENTRY_SIZE;
    itimer_entry = (uint8_t *)(ITIMER_DB_BASE + as_offset);

    /* Check if there's a repeat interval set */
    interval_high = *(uint32_t *)(itimer_entry + ITIMER_VIRT_INTERVAL_HIGH);
    interval_low = *(uint16_t *)(itimer_entry + ITIMER_VIRT_INTERVAL_LOW);

    if (interval_high != 0 || interval_low != 0) {
        /* Send SIGVTALRM to the process */
        void *uid = (void *)((char *)&PROC2_UID + (as_id << 3));
        uint16_t sig_num = SIGVTALRM;
        uint32_t sig_code = 0;
        PROC2_$SIGNAL_OS(uid, &sig_num, &sig_code, &status);
    }
}
