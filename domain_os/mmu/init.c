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

#include "mmu_internal.h"

void MMU_$INIT(void)
{
    if (M68020 != 0) {
        /* 68020+ CPU: Set up PTT addressing */
        VA_TO_PTT_OFFSET_MASK = 0x3FFC00;
        /* PTT shift = 1, ASID shift = 6 */
        /* These are stored at offsets 6 and 8 from the globals base */
        *(uint16_t*)((char*)&VA_TO_PTT_OFFSET_MASK + 4) = 1;
        *(uint16_t*)((char*)&VA_TO_PTT_OFFSET_MASK + 6) = 6;
    } else {
        /* 68010 CPU: Cache control not needed, make CACHE_$CLEAR a no-op
         * This copies the RTS instruction from this function to CACHE_$CLEAR */
        /* CACHE_$CLEAR_ENTRY = 0x4E75; */ /* RTS opcode */
    }
}
