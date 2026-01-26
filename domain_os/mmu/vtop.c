/*
 * MMU_$VTOP - Translate virtual address to physical page number
 *
 * Performs a reverse lookup in the MMU structures to find the
 * physical page number that maps to the given virtual address.
 * This uses the PTT and PMAPE hash chain to find the mapping.
 *
 * Original address: 0x00e2410e
 */

#include "mmu_internal.h"

uint32_t MMU_$VTOP(uint32_t va, status_$t *status)
{
    uint16_t saved_sr;
    uint16_t old_csr;
    uint32_t va_key;
    uint16_t *ptt;
    uint16_t head_ppn;
    uint16_t ppn;
    uint16_t first_ppn;
    uint32_t pmape_val;
    uint16_t pmape_high;

    /* Build the match key from the VA
     * The key encodes VA bits and current ASID for matching */
    va_key = va;
    uint8_t shift = *(uint8_t*)((char*)&VA_TO_PTT_OFFSET_MASK + 4);
    va_key <<= (shift & 0x3F);

    /* Insert current ASID */
    va_key = (va_key & 0xFFFF0000) | PROC1_$AS_ID;

    /* Rotate to match PMAPE format */
    va_key = (va_key >> 7) | (va_key << 25);
    va_key = (va_key >> 16) & 0xFFFF;

    /* Get PTT entry for this VA */
    ptt = PTT_FOR_VA(va);

    /* Disable interrupts and enable PTT access */
    DISABLE_INTERRUPTS(saved_sr);

    old_csr = MMU_$PID_PRIV;
    MMU_CSR = old_csr | CSR_PTT_ACCESS_BIT;

    /* Get head of hash chain */
    head_ppn = *ptt & PTT_PPN_MASK;

    if (head_ppn == 0) {
        /* No mapping exists */
        MMU_CSR = old_csr;
        SET_SR(saved_sr);
        *status = status_$mmu_miss;
        return 0;
    }

    /* Search the hash chain */
    ppn = head_ppn;
    first_ppn = head_ppn << 2;  /* Save for loop termination */

    do {
        pmape_val = *PMAPE_FOR_PPN(ppn);
        pmape_high = (uint16_t)(pmape_val >> 16);

        /* Check for match:
         * - Compare ASID and VA bits (mask 0xFE0F)
         * - Or if low nibble matches and global bit is set */
        uint16_t diff = pmape_high ^ (uint16_t)va_key;

        if ((diff & 0xFE0F) == 0) {
            /* Match found */
            MMU_CSR = old_csr;
            SET_SR(saved_sr);
            *status = status_$ok;
            return ppn;
        }

        if ((diff & 0x0F) == 0 && (pmape_val & PMAPE_FLAG_GLOBAL)) {
            /* Global match (ASID doesn't matter) */
            MMU_CSR = old_csr;
            SET_SR(saved_sr);
            *status = status_$ok;
            return ppn;
        }

        /* Follow hash chain */
        ppn = pmape_val & PMAPE_LINK_MASK;
    } while ((ppn << 2) != first_ppn);

    /* Not found in chain */
    MMU_CSR = old_csr;
    ENABLE_INTERRUPTS(saved_sr);
    *status = status_$mmu_miss;
    return 0;
}
