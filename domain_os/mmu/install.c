/*
 * MMU_$INSTALL - Install a virtual-to-physical mapping
 *
 * Creates a mapping from a virtual address to a physical page.
 * The mapping includes the ASID and protection bits, and sets
 * the global bit if the ASID indicates a shared mapping.
 *
 * Original address: 0x00e24048
 */

#include "mmu.h"

/* Forward declaration of internal install helper */
extern void mmu_$installi(uint16_t ppn, uint32_t va, uint32_t asid_prot);

void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint8_t asid, uint8_t prot)
{
    uint16_t saved_sr;
    uint16_t old_csr;
    uint32_t packed_info;

    /* Pack ASID and protection into the format expected by mmu_$installi:
     * The format encodes VA bits, ASID, and protection for PMAPE storage.
     *
     * D4 layout after packing:
     * - VA shifted by PTT shift value
     * - ASID in bits rotated into position
     * - Protection bits in low nibble
     */
    packed_info = va;

    /* Shift VA by the PTT shift value */
    uint8_t shift = *(uint8_t*)((char*)&VA_TO_PTT_OFFSET_MASK + 4);
    packed_info <<= (shift & 0x3F);

    /* Insert protection in bits 0-3 (after rotation) */
    packed_info = (packed_info & 0xFFFFFF00) | prot;

    /* Rotate right by 5 to position protection */
    packed_info = (packed_info >> 5) | (packed_info << 27);

    /* Insert ASID */
    packed_info = (packed_info & 0xFFFFFF00) | asid;

    /* Rotate right by 7 to final position */
    packed_info = (packed_info >> 7) | (packed_info << 25);

    /* For 68010, need additional shift */
    if (M68020 == 0) {
        packed_info = (packed_info & 0xFFFF0000) | ((packed_info & 0xFFFF) >> 2);
    }

    /* Mask off low nibble */
    packed_info &= ~0x0F;

    /* Disable interrupts and enable PTT access */
    DISABLE_INTERRUPTS(saved_sr);

    old_csr = MMU_$PID_PRIV;
    MMU_CSR = old_csr | CSR_PTT_ACCESS_BIT;

    /* Call internal installer */
    mmu_$installi((uint16_t)ppn, va, packed_info);

    /* Restore CSR and interrupts */
    MMU_CSR = old_csr;
    ENABLE_INTERRUPTS(saved_sr);
}

/*
 * mmu_$installi - Internal install helper
 *
 * Installs a mapping into the PTT and PMAPE structures.
 * Must be called with interrupts disabled and PTT access enabled.
 *
 * Original address: 0x00e2409c
 */
void mmu_$installi(uint16_t ppn, uint32_t va, uint32_t asid_prot)
{
    uint32_t *pmape = PMAPE_FOR_PPN(ppn);

    /* If there's an existing mapping, remove it first */
    if ((*(uint16_t*)((char*)pmape + 2) & PMAPE_LINK_MASK) != 0) {
        /* Remove existing mapping - forward declaration needed */
        extern void mmu_$remove_pmape(uint16_t ppn);
        mmu_$remove_pmape(ppn);
    }

    /* Store the ASID/VA info in the ASID table */
    ASID_TABLE_BASE[ppn] = (uint16_t)(asid_prot & 0xFFFF);

    /* Prepare PMAPE high word - set global bit if ASID is zero/shared */
    uint32_t pmape_high = asid_prot & 0xFFFF0000;
    if ((asid_prot & 0xFE000000) == 0) {
        pmape_high |= PMAPE_FLAG_GLOBAL;
    }

    /* Get PTT entry for this VA */
    uint16_t *ptt = PTT_FOR_VA(va);
    uint16_t head_ppn = *ptt & PTT_PPN_MASK;

    if (head_ppn == 0) {
        /* First entry for this PTT slot - set as head of chain */
        uint32_t new_pmape = (pmape_high & 0xFFFF0000) |
                            ((ppn ^ (asid_prot & 0xFFFF) | PMAPE_FLAG_HEAD) & 0xFFFF);
        *pmape ^= new_pmape;
        *ptt = ppn;
    } else {
        /* Add to existing chain */
        uint16_t *head_link = (uint16_t*)((char*)MMU_PMAPE_BASE + (head_ppn << 2) + 2);
        uint16_t head_link_val = *head_link;
        uint16_t next_ppn = head_link_val & PMAPE_LINK_MASK;

        /* Insert after head, linking to old next */
        uint32_t new_pmape = (pmape_high & 0xFFFF0000) |
                            ((next_ppn ^ (asid_prot & 0xFFFF)) & 0xFFFF);
        *pmape ^= new_pmape;

        /* Update head to point to new entry */
        *head_link = (ppn ^ next_ppn) ^ head_link_val;
    }
}
