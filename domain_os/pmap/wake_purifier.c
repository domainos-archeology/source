/*
 * PMAP_$WAKE_PURIFIER - Wake up the page purifier processes
 *
 * Signals the local and remote purifier processes to start cleaning
 * dirty pages. Optionally waits for pages to become available.
 *
 * Parameters:
 *   wait - If negative, wait for pages to become available
 *
 * Original address: 0x00e13a18
 */

#include "pmap.h"

/* External data */
extern uint32_t DAT_00e23344;  /* Remote purifier enabled flag */

void PMAP_$WAKE_PURIFIER(int8_t wait)
{
    ec_$eventcount_t *pages_ec;
    int32_t wait_value;

    /* Record the current pages event count + 1 */
    wait_value = PMAP_$PAGES_EC.value + 1;

    /* Wake local purifier */
    EC_$ADVANCE(&PMAP_$L_PURIFIER_EC);

    /* Wake remote purifier if enabled */
    if (DAT_00e23344 != 0) {
        EC_$ADVANCE(&PMAP_$R_PURIFIER_EC);
    }

    /* Optionally wait for pages to become available */
    if (wait < 0) {
        ML_$UNLOCK(PMAP_LOCK_ID);
        pages_ec = &PMAP_$PAGES_EC;
        EC_$WAITN(&pages_ec, &wait_value, 1);
        ML_$LOCK(PMAP_LOCK_ID);
    }
}
