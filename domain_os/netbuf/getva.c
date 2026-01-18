/*
 * NETBUF_$GETVA - Get virtual address for network buffer
 *
 * Maps a physical page to a virtual address in the network buffer space.
 *
 * Original address: 0x00E0EC78
 *
 * The VA slot array is used as:
 * - When slot is free: contains index of next free slot
 * - When slot is in use: contains the physical address (ppn << 10)
 *
 * Virtual addresses are computed as:
 *   va = VA_BASE + (slot_index * 0x400) + (ppn_shifted & 0x3FF)
 */

#include "netbuf/netbuf_internal.h"

void NETBUF_$GETVA(uint32_t ppn_shifted, uint32_t *va_out, status_$t *status)
{
    ml_$spin_token_t token;
    int32_t slot;
    uint32_t va;

    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    /* Check if free list is empty */
    if (NETBUF_$VA_TOP == -1) {
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
        *va_out = 0;
        *status = status_$network_out_of_blocks;
        return;
    }

    /* Remove slot from free list */
    slot = NETBUF_$VA_TOP;

    /* Compute virtual address:
     * VA = base + (slot * 1024) + (ppn_shifted & 0x3FF)
     */
    va = NETBUF_$VA_BASE_ADDR + ((uint32_t)slot << 10) + (ppn_shifted & 0x3FF);
    *va_out = va;

    /* Update free list head to next free slot */
    NETBUF_$VA_TOP = (int32_t)NETBUF_$VA_SLOTS[slot];

    /* Store physical address in the slot */
    NETBUF_$VA_SLOTS[slot] = ppn_shifted;

    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);

    *status = status_$ok;

    /* Install MMU mapping: ppn -> va with protection flags 0x16 */
    MMU_$INSTALL(ppn_shifted >> 10, va, 0x16);
}
