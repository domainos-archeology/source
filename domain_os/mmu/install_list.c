/*
 * MMU_$INSTALL_LIST - Install mappings for a list of pages
 *
 * Installs multiple contiguous virtual-to-physical mappings
 * efficiently in a single critical section.
 *
 * Parameters:
 *   count - Number of pages to map
 *   ppn_array - Array of physical page numbers
 *   va - Starting virtual address
 *   flags - Packed flags: byte 1 = ASID, byte 3 = protection
 *           Use MMU_FLAGS(asid, prot) macro to construct
 *
 * Original address: 0x00e23fde
 */

#include "mmu/mmu_internal.h"

void MMU_$INSTALL_LIST(uint16_t count, uint32_t *ppn_array, uint32_t va, uint32_t flags)
{
    uint16_t saved_sr;
    uint16_t old_csr;
    uint32_t packed_base;
    uint32_t packed_info;
    int16_t i;
    uint8_t prot;
    uint8_t asid;

    if (count == 0) return;

    /* Extract ASID and protection from packed flags */
    asid = (flags >> 16) & 0xFF;
    prot = flags & 0xFF;

    /* Pack ASID and protection for the base address */
    packed_base = va;

    /* 0xE23FF2: move.w MMU_$PTT_SHIFT,D1w / 0xE23FF6: lsl.l D1,D5 */
    packed_base <<= (MMU_$PTT_SHIFT & 0x3F);

    packed_base = (packed_base & 0xFFFFFF00) | prot;
    packed_base = (packed_base >> 5) | (packed_base << 27);
    packed_base = (packed_base & 0xFFFFFF00) | asid;
    packed_base = (packed_base >> 7) | (packed_base << 25);

    /* 0xE24004: tst.w M68020 / bne - whole-word test */
    if (!M68020_IS_020_W()) {
        packed_base = (packed_base & 0xFFFF0000) | ((packed_base & 0xFFFF) >> 2);
    }

    packed_base &= ~0x0F;

    /* Disable interrupts and enable PTT access */
    DISABLE_INTERRUPTS(saved_sr);

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
    ENABLE_INTERRUPTS(saved_sr);
}
