/*
 * MST_$FIND - Find physical address for virtual address
 *
 * This function looks up the physical address for a given virtual address.
 * If the page is already mapped, it returns the physical address directly.
 * If not mapped, it calls MST_$TOUCH to fault in the page.
 *
 * The flags parameter controls behavior:
 * - Bit 0: Must be 0 (assertion check)
 * - Bit 1: Wire the page after finding
 * - Bit 2: Must be 0 (assertion check)
 *
 * Bits 0 and 2 being set causes a system crash, indicating this function
 * should not be called with those flags.
 */

#include "mst/mst_internal.h"
#include "misc/misc.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/* 0x00E0E130: pea (0x86,PC) -> 0x00E0E1B8, jsr CRASH_SYSTEM at 0x00E0E134. */
static const status_$t mst_$ref_out_of_bounds_00e0e1b8 = 0x00040005;

/*
 * MST_$FIND - Find physical address for virtual address
 *
 * @param virt_addr  Virtual address to look up
 * @param flags      Control flags (bit 1 = wire page)
 * @return Physical address, or result from MST_$TOUCH if not mapped
 */
uint32_t MST_$FIND(uint32_t virt_addr, uint16_t flags)
{
    uint32_t phys_addr;
    status_$t status[2];

    /*
     * Check for invalid flags - bits 0 and 2 must be clear.
     * This catches programming errors where caller passes wrong flags.
     */
    if ((flags & 5) != 0) {
        CRASH_SYSTEM(&mst_$ref_out_of_bounds_00e0e1b8);
    }

    /* Lock MMU for translation */
    ML_$LOCK(MST_LOCK_MMU);

    /* Try to translate virtual to physical address */
    phys_addr = MMU_$VTOP(virt_addr, status);

    if (status[0] == status_$ok) {
        /*
         * Page is already mapped.
         * Wire it if requested (bit 1 set).
         */
        if ((flags & 2) != 0) {
            MMAP_$WIRE(phys_addr);
        }
        ML_$UNLOCK(MST_LOCK_MMU);
        return phys_addr;
    }

    /* Page not mapped - unlock and call MST_$TOUCH to fault it in */
    ML_$UNLOCK(MST_LOCK_MMU);

    /* Call MST_$TOUCH with wire flag based on bit 1 of flags */
    return MST_$TOUCH(virt_addr, status, (flags & 2) != 0 ? 1 : 0);
}
