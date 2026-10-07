/*
 * MMU_$SET_CSR - Set the ASID byte of MMU_$PID_PRIV and write the CSR
 *
 * 0x00E241F4 - 0x00E24202 (16 bytes, hand-written `MMU_ASM`).  Re-emitted
 * 2026-09-27: the earlier C stored the argument's HIGH byte; the image
 * stores `(5,SP)` - the LOW byte of the word argument - into the HIGH
 * byte of MMU_$PID_PRIV (`move.b (0x5,SP),(A0)` with A0 = 0xE23D2C),
 * then writes the whole word to the CSR.
 *
 * The m68k build assembles mmu/sau2/set_csr.s (byte-checked); this is the
 * host-side model.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void (MMU_$SET_CSR)(uint32_t csr_val_slot)
{
    uint16_t csr_val = ARCH_PASCAL_SLOT_WORD(csr_val_slot); /* only its low byte, (5,SP), is read */
    MMU_$PID_PRIV = (uint16_t)(((csr_val & 0xFF) << 8) | (MMU_$PID_PRIV & 0x00FF)); /* 0x00E241F8 */
    MMU_CSR = MMU_$PID_PRIV;                                                     /* 0x00E241FC */
}

#endif /* !ARCH_M68K */
