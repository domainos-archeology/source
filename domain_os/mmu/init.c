/*
 * MMU_$INIT - Initialize MMU subsystem
 *
 * Initializes the MMU hardware abstraction layer. Sets up the
 * VA-to-PTT mask and shift values based on CPU type (68010 vs 68020+).
 *
 * For 68010: Modifies CACHE_$CLEAR to be a no-op by copying RTS instruction
 * For 68020+: Sets up VA_TO_PTT_OFFSET_MASK and shift values
 *
 * Original address: 0x00e23d38
 */

#include "mmu/mmu_internal.h"

void MMU_$INIT(void)
{
    /* 0xE23D3E: tst.w (A5) / beq - whole-word test of M68020 */
    if (M68020_IS_020_W()) {
        /* 68020+ CPU: Set up PTT addressing */
        VA_TO_PTT_OFFSET_MASK = 0x3FFC00;   /* 0xE23D42: move.l #0x3ffc00,(0x2,A5) */
        MMU_$VA_SHIFT = 1;                  /* 0xE23D4A: move.w #0x1,(0x6,A5) */
        MMU_$PTT_SHIFT = 6;                 /* 0xE23D50: move.w #0x6,(0x8,A5) */
    } else {
        /* 68010 CPU: Cache control not needed, make CACHE_$CLEAR a no-op
         * This copies the RTS instruction from this function to CACHE_$CLEAR */
        /* CACHE_$CLEAR_ENTRY = 0x4E75; */ /* RTS opcode */
    }
}
