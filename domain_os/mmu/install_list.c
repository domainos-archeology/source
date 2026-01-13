/*
 * MMU_$INSTALL_LIST - Install mappings for a list of pages
 *
 * Installs multiple contiguous virtual-to-physical mappings
 * efficiently in a single critical section.
 *
 * Original address: 0x00e23fde
 */

#include "mmu.h"

/* Forward declaration of internal install helper */
extern void mmu_$installi(uint16_t ppn, uint32_t va, uint32_t asid_prot);

void MMU_$INSTALL_LIST(uint16_t count, uint32_t *ppn_array, uint32_t va,
                       uint8_t asid, uint8_t prot)
{
    uint16_t saved_sr;
    uint16_t old_csr;
    uint32_t packed_base;
    uint32_t packed_info;
    int16_t i;

    if (count == 0) return;

    /* Pack ASID and protection for the base address */
    packed_base = va;

    uint8_t shift = *(uint8_t*)((char*)&VA_TO_PTT_OFFSET_MASK + 4);
    packed_base <<= (shift & 0x3F);

    packed_base = (packed_base & 0xFFFFFF00) | prot;
    packed_base = (packed_base >> 5) | (packed_base << 27);
    packed_base = (packed_base & 0xFFFFFF00) | asid;
    packed_base = (packed_base >> 7) | (packed_base << 25);

    if (M68020 == 0) {
        packed_base = (packed_base & 0xFFFF0000) | ((packed_base & 0xFFFF) >> 2);
    }

    packed_base &= ~0x0F;

    /* Disable interrupts and enable PTT access */
    saved_sr = GET_SR();
    SET_SR(saved_sr | SR_IPL_DISABLE_ALL);

    old_csr = MMU_$PID_PRIV;
    MMU_CSR = old_csr | CSR_PTT_ACCESS_BIT;

    /* Install each mapping */
    packed_info = packed_base;
    for (i = count - 1; i >= 0; i--) {
        mmu_$installi((uint16_t)*ppn_array, va, packed_info);

        ppn_array++;
        packed_info += 0x10;  /* Increment protection/offset field */
        va += 0x400;          /* Next 1KB page */
    }

    /* Restore CSR and interrupts */
    MMU_CSR = old_csr;
    SET_SR(saved_sr);
}
