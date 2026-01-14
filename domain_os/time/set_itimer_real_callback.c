/*
 * TIME_$SET_ITIMER_REAL_CALLBACK - Callback for real-time itimer
 *
 * Called when the real-time interval timer expires.
 * Sends SIGALRM to the process if the timer has an interval set.
 *
 * Parameters:
 *   arg - Pointer to callback argument containing AS info
 *
 * Original address: 0x00e58a38
 */

#include "time.h"
#include "proc2/proc2.h"

/* Signal numbers */
#define SIGALRM 14

/* itimer database base */
#define ITIMER_DB_BASE      0xE297F0
#define ITIMER_DB_ENTRY_SIZE 0x1C

/* Offsets */
#define ITIMER_REAL_INTERVAL_HIGH   0x0C
#define ITIMER_REAL_INTERVAL_LOW    0x10

/* Process UID array */
extern uint8_t PROC2_UID[];

/* External signal function */
extern void PROC2_$SIGNAL_OS(void *uid, void *sig_num, void *sig_code, status_$t *status);

void TIME_$SET_ITIMER_REAL_CALLBACK(void *arg)
{
    uint32_t **arg_ptr = (uint32_t **)arg;
    uint32_t *inner = *arg_ptr;
    uint16_t as_id = (uint16_t)inner[0];  /* AS ID at offset 0x02 in 16-bit */
    int16_t as_offset;
    uint8_t *itimer_entry;
    uint32_t interval_high;
    uint16_t interval_low;
    status_$t status;

    /* Get the itimer entry for this AS */
    as_offset = as_id * ITIMER_DB_ENTRY_SIZE;
    itimer_entry = (uint8_t *)(ITIMER_DB_BASE + as_offset);

    /* Check if there's a repeat interval set */
    interval_high = *(uint32_t *)(itimer_entry + ITIMER_REAL_INTERVAL_HIGH);
    interval_low = *(uint16_t *)(itimer_entry + ITIMER_REAL_INTERVAL_LOW);

    if (interval_high != 0 || interval_low != 0) {
        /* Send SIGALRM to the process */
        void *uid = &PROC2_UID[as_id * 8];
        uint16_t sig_num = SIGALRM;
        uint32_t sig_code = 0;
        PROC2_$SIGNAL_OS(uid, &sig_num, &sig_code, &status);
    }
}
