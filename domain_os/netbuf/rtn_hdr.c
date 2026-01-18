/*
 * NETBUF_$RTN_HDR - Return a header buffer
 *
 * Returns a header buffer to the free pool.
 *
 * Original address: 0x00E0EEB4
 *
 * This function:
 * 1. Validates the buffer VA is in the expected range
 * 2. Links the buffer back into the HDR_TOP free list
 */

#include "netbuf/netbuf_internal.h"

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    uint32_t va;
    int32_t offset;
    ml_$spin_token_t token;

    va = *va_ptr;

    /* Validate VA is in the netbuf range */
    offset = (int32_t)(va - NETBUF_$VA_BASE_ADDR);
    if (offset < 0) {
        offset = offset + 0x3FF;  /* Round toward -infinity */
    }
    offset = offset >> 10;

    if (offset < 0 || offset > (NETBUF_VA_SLOTS - 1)) {
        CRASH_SYSTEM(&netbuf_err);
    }

    /* Acquire lock and add to free list */
    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    /* Link buffer into free list (using 1KB aligned address) */
    va = va & ~0x3FF;  /* Align to 1KB boundary */
    NETBUF_HDR_NEXT(va) = NETBUF_$HDR_TOP;
    NETBUF_$HDR_TOP = va;

    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
}
