/*
 * MMU_$REMOVE_LIST - Unlink an array of physical pages
 *
 * 0x00E23D92 - 0x00E23DCA (58 bytes, hand-written `MMU_ASM`).  Re-emitted
 * 2026-09-27: the earlier C had an invented `count == 0` return; the
 * image does `subq.w #1` / `dbf`, so a zero count removes 65536 pages.
 *
 * The m68k build assembles mmu/sau2/remove_list.s (byte-checked); this is
 * the host-side model.
 *
 * Arguments: (0x20,SP) ppn_array -> A4 (longwords, low word used),
 * (0x24,SP) count word -> D7.  SR in D6, IPL 7, CSR := MMU_$PID_PRIV | 2,
 * mmu_$remove_internal per page, CSR := MMU_$PID_PRIV, SR restored.
 */

#include "mmu/mmu_internal.h"

#if !defined(ARCH_M68K)

void (MMU_$REMOVE_LIST)(uint32_t *ppn_array, uint32_t count_slot)
{
    uint16_t count = ARCH_PASCAL_SLOT_WORD(count_slot);  /* (0x24,SP) after the movem */
    uint16_t saved_sr;              /* D6 */
    uint16_t n = (uint16_t)(count - 1); /* D7 */

    DISABLE_INTERRUPTS(saved_sr);                       /* 0x00E23DA0 */
    MMU_CSR = MMU_$PID_PRIV | CSR_PTT_ACCESS_BIT;       /* 0x00E23DA6 */
    do {                                                /* 0x00E23DB4 - 0x00E23DB8 */
        mmu_$remove_internal((uint16_t)*ppn_array);
        ppn_array++;
    } while (n-- != 0);
    MMU_CSR = MMU_$PID_PRIV;                            /* 0x00E23DBC */
    ENABLE_INTERRUPTS(saved_sr);                        /* 0x00E23DC4 */
}

#endif /* !ARCH_M68K */
