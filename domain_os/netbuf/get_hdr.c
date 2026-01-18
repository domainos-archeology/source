/*
 * NETBUF_$GET_HDR_COND and NETBUF_$GET_HDR - Get header buffer
 *
 * NETBUF_$GET_HDR_COND: Non-blocking, returns 0 if pool is empty
 * NETBUF_$GET_HDR: Blocking, waits or allocates if pool is empty
 *
 * Original addresses:
 *   NETBUF_$GET_HDR_COND: 0x00E0ED6C
 *   NETBUF_$GET_HDR:      0x00E0EDD6
 */

#include "netbuf/netbuf_internal.h"

/*
 * NETBUF_$GET_HDR_COND - Conditionally get a header buffer
 *
 * Attempts to get a header buffer without blocking.
 * Returns the physical address and virtual address of the buffer.
 */
int8_t NETBUF_$GET_HDR_COND(uint32_t *phys_out, uint32_t *va_out)
{
    ml_$spin_token_t token;
    uint32_t va;

    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    *va_out = NETBUF_$HDR_TOP;
    va = *va_out;

    if (va == 0) {
        /* Pool is empty */
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
        return 0;
    }

    /* Remove from free list */
    NETBUF_$HDR_TOP = NETBUF_HDR_NEXT(va);

    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);

    /* Return physical address from buffer */
    *phys_out = NETBUF_HDR_PHYS(va);

    return (int8_t)-1;  /* true = success */
}

/*
 * NETBUF_$GET_HDR - Get a header buffer (blocking)
 *
 * Gets a header buffer, blocking if none are available.
 * If running as a type-7 (network) process, waits on the delay queue.
 * Otherwise allocates a new buffer directly.
 */
void NETBUF_$GET_HDR(uint32_t *phys_out, uint32_t *va_out)
{
    int8_t result;
    status_$t status;
    uint32_t ppn;
    uint32_t va;
    int16_t proc_type;
    int i;

    while (1) {
        /* Try to get buffer from pool */
        result = NETBUF_$GET_HDR_COND(phys_out, va_out);
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

        NETBUF_$HDR_DELAYS++;
    }

    /* Allocate a new buffer */
    WP_$CALLOC(&ppn, &status);
    *phys_out = ppn << 10;

    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* Get virtual address mapping */
    NETBUF_$GETVA(ppn << 10, va_out, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    NETBUF_$HDR_ALLOCS++;

    /* Initialize the buffer data area (offsets 0x3ec-0x3fb) */
    va = *va_out;
    for (i = 0; i < 4; i++) {
        *(uint32_t *)(va + 0x3ec + i * 4) = 0;
    }

    /* Store physical address */
    NETBUF_HDR_PHYS(va) = *phys_out;
}
