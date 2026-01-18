/*
 * NETBUF_$RTNVA - Return virtual address for network buffer
 *
 * Unmaps a virtual address previously obtained from NETBUF_$GETVA.
 *
 * Original address: 0x00E0ED26
 *
 * This is a wrapper that acquires the spin lock and calls the
 * internal netbuf_rtnva_locked function.
 */

#include "netbuf/netbuf_internal.h"

uint32_t NETBUF_$RTNVA(uint32_t *va_ptr)
{
    ml_$spin_token_t token;
    uint32_t ppn;

    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);
    ppn = netbuf_rtnva_locked(va_ptr);
    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);

    return ppn;
}

/*
 * netbuf_rtnva_locked - Return VA slot to free list (caller holds lock)
 *
 * Original address: 0x00E0E8C4
 *
 * This function:
 * 1. Computes slot index from VA
 * 2. Validates slot index is in range [0, 191]
 * 3. Gets physical address stored in slot
 * 4. Removes MMU mapping
 * 5. Adds slot back to free list
 * 6. Returns physical address
 */
uint32_t netbuf_rtnva_locked(uint32_t *va_ptr)
{
    uint32_t va;
    int32_t slot;
    uint32_t ppn;

    va = *va_ptr;

    /* Compute slot index from VA:
     * slot = (va - VA_BASE) / 1024
     * Using signed arithmetic with rounding toward negative infinity
     */
    slot = (int32_t)(va - NETBUF_$VA_BASE_ADDR);
    if (slot < 0) {
        slot = slot + 0x3FF;  /* Round toward -infinity */
    }
    slot = slot >> 10;

    /* Validate slot index */
    if (slot < 0 || slot > (NETBUF_VA_SLOTS - 1)) {
        CRASH_SYSTEM(&netbuf_err);
    }

    /* Get physical address from slot */
    ppn = NETBUF_$VA_SLOTS[slot];

    /* Remove MMU mapping */
    MMU_$REMOVE(ppn >> 10);

    /* Add slot back to free list */
    NETBUF_$VA_SLOTS[slot] = (uint32_t)NETBUF_$VA_TOP;
    NETBUF_$VA_TOP = slot;

    return ppn;
}
