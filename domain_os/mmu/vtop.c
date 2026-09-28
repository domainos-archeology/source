/*
 * MMU_$VTOP - Virtual address to physical page number
 *
 * 0x00E2410E - 0x00E241B6 (170 bytes, hand-written `MMU_ASM`).  Verified
 * against the disassembly 2026-09-27; the earlier C was faithful.  The
 * m68k build assembles mmu/sau2/vtop.s (byte-checked); this is the
 * host-side model.
 *
 * Arguments: (0x10,SP) va, (0x14,SP) status -> 0 on a hit, 0x00070001
 * (status_$mmu_miss) on a miss.  Result D0 = ppn, or 0.
 *
 * D5 = ((va << MMU_$VA_SHIFT) with PROC1_$AS_ID in the low word) ror 7,
 * swapped: its low word is the key.  Under a saved-SR IPL-7 bracket with
 * PTT access on, the ring at the PTT entry is walked from its head: an
 * entry whose high word matches the key under 0xfe0f is a hit, and so is
 * one matching under 0x000f with bit 12 (GLOBAL) set; the walk stops when
 * the link comes back to the head.
 */

#include "mmu/mmu_internal.h"
#include "proc1/proc1.h"

#if !defined(ARCH_M68K)

uint32_t MMU_$VTOP(uint32_t va, status_$t *status)
{
    uint16_t saved_sr;
    uint32_t d5;                    /* D5 */
    uint16_t key;
    uint16_t *ptt;                  /* A1 */
    uint16_t d0, d4;                /* D0w, D4w: entry index * 4 */
    uint32_t d3;                    /* D3 */
    uint16_t d1;

    /* 0x00E24112 - 0x00E24132 */
    ptt = (uint16_t *)((char *)PTT_BASE + (va & VA_TO_PTT_OFFSET_MASK));
    d5 = va << (MMU_$VA_SHIFT & 0x3F);
    d5 = (d5 & 0xFFFF0000u) | PROC1_$AS_ID;
    d5 = (d5 >> 7) | (d5 << 25);
    key = (uint16_t)(d5 >> 16);

    /* 0x00E24134 - 0x00E24146 */
    DISABLE_INTERRUPTS(saved_sr);
    MMU_CSR = MMU_$PID_PRIV | CSR_PTT_ACCESS_BIT;

    /* 0x00E24148 - 0x00E24150 */
    d0 = (uint16_t)(*ptt & 0x0FFF);
    if (d0 != 0) {
        d0 = (uint16_t)(d0 << 2);
        d4 = d0;
        for (;;) {
            /* 0x00E2415C - 0x00E24182 */
            d3 = *(uint32_t *)((char *)PFT_BASE + (int16_t)d0);
            d1 = (uint16_t)((d3 >> 16) ^ key);
            if ((d1 & 0xFE0F) == 0) {
                goto hit;
            }
            if ((d1 & 0x000F) == 0 && (d3 & PMAPE_FLAG_GLOBAL) != 0) {
                goto hit;
            }
            d0 = (uint16_t)((d3 & 0x0FFF) << 2);
            if (d0 == d4) {
                break;
            }
        }
    }

    /* 0x00E24184 - 0x00E2419E */
    MMU_CSR = MMU_$PID_PRIV;
    ENABLE_INTERRUPTS(saved_sr);
    *status = status_$mmu_miss;
    return 0;

hit:
    /* 0x00E241A0 - 0x00E241B6 */
    MMU_CSR = MMU_$PID_PRIV;
    ENABLE_INTERRUPTS(saved_sr);
    *status = status_$ok;
    return (uint16_t)(d0 >> 2);
}

#endif /* !ARCH_M68K */
