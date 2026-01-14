/*
 * MMU_$INSTALL_PRIVATE - Install a private mapping
 *
 * Creates a mapping without the global bit set, meaning this
 * mapping is private to a single address space. After installing,
 * it clears the global bit in the PMAPE entry.
 *
 * Parameters:
 *   ppn - Physical page number
 *   va - Virtual address
 *   flags - Packed flags: byte 1 = ASID, byte 3 = protection
 *           Use MMU_FLAGS(asid, prot) macro to construct
 *
 * Original address: 0x00e23f82
 */

#include "mmu.h"

/* Forward declaration of internal install helper */
extern void mmu_$installi(uint16_t ppn, uint32_t va, uint32_t packed_info);

void MMU_$INSTALL_PRIVATE(uint32_t ppn, uint32_t va, uint32_t flags)
{
    uint16_t saved_sr;
    uint16_t old_csr;
    uint32_t packed_info;
    uint32_t *pmape;
    uint8_t prot;
    uint8_t asid;

    /* Extract ASID and protection from packed flags */
    asid = (flags >> 16) & 0xFF;
    prot = flags & 0xFF;

    /* Pack ASID and protection (same as MMU_$INSTALL) */
    packed_info = va;

    uint8_t shift = *(uint8_t*)((char*)&VA_TO_PTT_OFFSET_MASK + 4);
    packed_info <<= (shift & 0x3F);

    packed_info = (packed_info & 0xFFFFFF00) | prot;
    packed_info = (packed_info >> 5) | (packed_info << 27);
    packed_info = (packed_info & 0xFFFFFF00) | asid;
    packed_info = (packed_info >> 7) | (packed_info << 25);

    if (M68020 == 0) {
        packed_info = (packed_info & 0xFFFF0000) | ((packed_info & 0xFFFF) >> 2);
    }

    packed_info &= ~0x0F;

    /* Disable interrupts and enable PTT access */
    DISABLE_INTERRUPTS(saved_sr);

    old_csr = MMU_$PID_PRIV;
    MMU_CSR = old_csr | CSR_PTT_ACCESS_BIT;

    /* Call internal installer */
    mmu_$installi((uint16_t)ppn, va, packed_info);

    /* Clear the global bit - this mapping is private */
    pmape = PMAPE_FOR_PPN(ppn);
    *(uint16_t*)((char*)pmape + 2) &= ~PMAPE_FLAG_GLOBAL;

    /* Restore CSR and interrupts */
    MMU_CSR = old_csr;
    ENABLE_INTERRUPTS(saved_sr);
}
