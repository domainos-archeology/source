/*
 * NETBUF_$GET_DAT_COND and NETBUF_$GET_DAT - Get data buffer
 *
 * NETBUF_$GET_DAT_COND: Non-blocking, returns 0 if pool is empty
 * NETBUF_$GET_DAT: Blocking, waits or allocates if pool is empty
 *
 * Original addresses:
 *   NETBUF_$GET_DAT_COND: 0x00E0EF28
 *   NETBUF_$GET_DAT:      0x00E0EFA4
 *
 * Data buffers are tracked by page number (PPN). The free list
 * uses the MMAPE next_vpn field to link pages together.
 */

#include "netbuf/netbuf_internal.h"

/*
 * NETBUF_$GET_DAT_COND - Conditionally get a data buffer
 *
 * Attempts to get a data buffer without blocking.
 * Returns the buffer address (ppn << 10).
 */
int8_t NETBUF_$GET_DAT_COND(uint32_t *addr_out)
{
    ml_$spin_token_t token;
    uint32_t ppn;

    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    if (NETBUF_$DAT_CNT == 0) {
        /* Pool is empty */
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
        *addr_out = 0;
        return 0;
    }

    /* Get page from free list */
    ppn = NETBUF_$DAT_TOP;

    /* Update free list head */
    NETBUF_$DAT_TOP = NETBUF_DAT_NEXT(ppn);
    NETBUF_$DAT_CNT--;

    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);

    *addr_out = ppn << 10;
    return (int8_t)-1;  /* true = success */
}

/*
 * NETBUF_$GET_DAT - Get a data buffer (blocking)
 *
 * Gets a data buffer, blocking if none are available.
 * If running as a type-7 (network) process, waits on the delay queue.
 * Otherwise allocates a new page directly.
 */
void NETBUF_$GET_DAT(uint32_t *addr_out)
{
    int8_t result;
    status_$t status;
    uint32_t ppn;
    int16_t proc_type;

    while (1) {
        /* Try to get buffer from pool */
        result = NETBUF_$GET_DAT_COND(addr_out);
        if (result < 0) {
            return;  /* Got one */
        }

        /* Check if we're a network process (type 7) */
        proc_type = PROC1_$TYPE[PROC1_$CURRENT_PCB->mypid];

        if (proc_type != NETBUF_NETWORK_PROC_TYPE) {
            /* Non-network process: allocate directly */
            break;
        }

        /* Network process: wait for buffer to become available */
        TIME_$WAIT(&NETBUF_$DELAY_TYPE, NETBUF_$DELAY_Q, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        NETBUF_$DAT_DELAYS++;
    }

    /* Allocate a new page */
    WP_$CALLOC(&ppn, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    NETBUF_$DAT_ALLOCS++;

    *addr_out = ppn << 10;
}
