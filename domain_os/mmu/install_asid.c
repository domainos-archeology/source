/*
 * MMU_$INSTALL_ASID - Switch the MMU to an address space
 *
 * 0x00E24204 - 0x00E24228 (38 bytes, in the hand-written `MMU_ASM` segment
 * 0xE23D2C..0xE248E3; no frame, argument read straight off the stack).
 * Verified against the disassembly on 2026-09-22; the earlier emission was
 * faithful.  The routine is register-level assembly transcribed as C so the
 * host build can exercise it; a byte-identical mmu/sau2 transcription is
 * tracked separately (see the bead in the report).
 *
 *   0x00E24204  move.w (0x4,SP),D1w              asid (word argument)
 *   0x00E24208  move.w D1w,(0xE2060A)            PROC1_$AS_ID
 *   0x00E2420E  move.b D1b,(0xE23D2C)            HIGH byte of MMU_$PID_PRIV
 *   0x00E24214  move.w (MMU_$PID_PRIV,PC),(0xFFB400)   CSR
 *   0x00E2421C  move.b (0xE218D5),(0xFFB402)     low byte of FP_$OWNER into
 *                                                the FPU owner register
 *   0x00E24226  bra.w CACHE_$CLEAR               tail call
 *
 * The m68k build assembles mmu/sau2/install_asid.s (byte-checked against the
 * image); this file is the host-side model of that routine, compiled only
 * for the host build so the unit tests can drive it.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void MMU_$INSTALL_ASID(uint16_t asid)
{
    PROC1_$AS_ID = asid;                                            /* 0x00E24208 */
    MMU_$PID_PRIV = (uint16_t)(((asid & 0xFF) << 8) | (MMU_$PID_PRIV & 0x00FF)); /* 0x00E2420E */
    MMU_CSR = MMU_$PID_PRIV;                                        /* 0x00E24214 */
    MMU_POWER_REG_BYTE = (uint8_t)FP_$OWNER;                        /* 0x00E2421C */
    CACHE_$CLEAR();                                                 /* 0x00E24226 */
}

#endif /* !ARCH_M68K */
