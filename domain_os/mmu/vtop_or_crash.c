/*
 * mmu_$vtop_or_crash - Translate VA to PA, crash on failure
 *
 * Wrapper around MMU_$VTOP that crashes the system if the
 * translation fails. Used in contexts where a valid mapping
 * is required.
 *
 * Original address: 0x00e3190c
 */

#include "mmu.h"
#include "misc/misc.h"

uint32_t mmu_$vtop_or_crash(uint32_t va)
{
    uint32_t ppn;
    status_$t status;

    ppn = MMU_$VTOP(va, &status);

    if (status != status_$ok) {
        CRASH_SYSTEM("MMU translation failed");
    }

    return ppn;
}
