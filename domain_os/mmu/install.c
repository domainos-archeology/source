/*
 * MMU_$INSTALL - Install a virtual-to-physical mapping
 *
 * Creates a mapping from a virtual address to a physical page.
 * The flags parameter contains packed ASID and protection bits.
 * Sets the global bit if the ASID indicates a shared mapping.
 *
 * Parameters:
 *   ppn - Physical page number
 *   va - Virtual address
 *   flags - Packed flags: byte 1 = ASID, byte 3 = protection
 *           Use MMU_FLAGS(asid, prot) macro to construct
 *
 * Original address: 0x00e24048
 */

#include "mmu.h"

/* Forward declaration of internal install helper */
extern void mmu_$installi(uint16_t ppn, uint32_t va, uint32_t packed_info);

void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags)
{
    uint16_t saved_sr;
    uint32_t packed_info;
    uint8_t prot;
    uint8_t asid;

    /*
     * Extract ASID and protection from packed flags.
     * On big-endian M68K, a 32-bit value 0xAABBCCDD has:
     *   byte 1 (offset +1) = BB = asid
     *   byte 3 (offset +3) = DD = prot
     *
     * So flags = (asid << 16) | prot, meaning:
     *   asid = (flags >> 16) & 0xFF
     *   prot = flags & 0xFF
     */
    asid = (flags >> 16) & 0xFF;
    prot = flags & 0xFF;

    /*
     * Build the packed_info value that mmu_$installi expects.
     * This encodes VA bits, ASID, and protection for PMAPE storage.
     *
     * The assembly does:
     * 1. D4 = va << shift
     * 2. D4.b = prot; ror.l #5,D4
     * 3. D4.b = asid; ror.l #7,D4
     * 4. (68010 only) D4.w >>= 2
     * 5. D4 &= ~0x0F
     */
    packed_info = va;

    /* Shift VA by the PTT shift value */
    uint8_t shift = *((uint8_t*)&VA_TO_PTT_OFFSET_MASK + 4);  /* PTT shift at offset +4 */
    packed_info <<= (shift & 0x3F);

    /* Insert protection in low byte, then rotate right 5 */
    packed_info = (packed_info & 0xFFFFFF00) | prot;
    packed_info = (packed_info >> 5) | (packed_info << 27);

    /* Insert ASID in low byte, then rotate right 7 */
    packed_info = (packed_info & 0xFFFFFF00) | asid;
    packed_info = (packed_info >> 7) | (packed_info << 25);

    /* For 68010, need additional shift of low word */
    if (M68020 == 0) {
        packed_info = (packed_info & 0xFFFF0000) | ((packed_info & 0xFFFF) >> 2);
    }

    /* Mask off low nibble */
    packed_info &= ~0x0F;

    /* Disable interrupts and enable PTT access */
    DISABLE_INTERRUPTS(saved_sr);

    uint16_t old_csr = MMU_$PID_PRIV;
    MMU_CSR = old_csr | CSR_PTT_ACCESS_BIT;

    /* Call internal installer */
    mmu_$installi((uint16_t)ppn, va, packed_info);

    /* Restore CSR and interrupts */
    MMU_CSR = old_csr;
    ENABLE_INTERRUPTS(saved_sr);
}
