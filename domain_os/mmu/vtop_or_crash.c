/*
 * mmu_$vtop_or_crash - Translate VA to PA, crash on failure
 *
 * Wrapper around MMU_$VTOP that crashes the system if the
 * translation fails. Used in contexts where a valid mapping
 * is required.
 *
 * Original address: 0x00e3190c
 *
 * Verified against the disassembly 2026-09-27 (0x00E3190C - 0x00E3193C):
 * link -0xc, D2 saved, MMU_$VTOP(va, &(-0x8,A6)) -> D2, a non-zero status
 * is handed to CRASH_SYSTEM by reference, D2 returned.  Compiled C, not
 * MMU_ASM: it is the module-local helper that opens the `I E3190C MMAP_UNWIRED` segment, just before MMAP_$INIT.
 */

#include "mmu/mmu_internal.h"
#include "misc/misc.h"

uint32_t mmu_$vtop_or_crash(uint32_t va)
{
    uint32_t ppn;
    status_$t status;

    ppn = MMU_$VTOP(va, &status);

    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    return ppn;
}
